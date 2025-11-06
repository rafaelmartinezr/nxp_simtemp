
/******************** MACROS ********************/

#define NXP_SIMTEMP_MINOR_COUNT 1
#define NXP_SIMTEMP_DRIVER_NAME "nxp_simtemp"
#define NXP_SIMTEMP_CLASS_NAME "nxp_simtemp"
#define NXP_SIMTEMP_DEVICE_NAME "simtemp"

#define pr_fmt(fmt) NXP_SIMTEMP_DRIVER_NAME ": " fmt

/******************** INCLUDES ********************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/ktime.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#include "nxp_simtemp.h"
#include "nxp_simtemp_buffer.h"
#include "nxp_simtemp_generators.h"

/******************** DATA TYPES ********************/

typedef struct nxp_simtemp_device {
        dev_t devnum;
        struct device *device;
        struct cdev cdev;
} nxp_simtemp_dev_t;

/******************** FUNCTION PROTOTYPES ********************/

static int nxp_simtemp_open(struct inode *inode, struct file *file);
static ssize_t nxp_simtemp_read(struct file *file, char __user *out_buff, 
                                size_t req_len, loff_t *loff);
static int nxp_simtemp_release(struct inode *inode, struct file *file);

static void generate_temperature(struct timer_list *data);

/******************** PUBLIC CONST ********************/

/******************** PRIVATE CONST ********************/

static const struct class nxp_simtemp_class = {
        .name = NXP_SIMTEMP_CLASS_NAME,
};

static const struct file_operations nxp_simtemp_fops = {
    .owner = THIS_MODULE,
    .open = nxp_simtemp_open,
    .read = nxp_simtemp_read,
    .release = nxp_simtemp_release,
};

/******************** PUBLIC VARIABLES ********************/

/******************** STATIC VARIABLES ********************/

static nxp_simtemp_dev_t simtemp_dev;

static struct timer_list nxp_simtemp_tmr;
static DECLARE_WAIT_QUEUE_HEAD(nxp_simtemp_wq);
static atomic_t entry_available = ATOMIC_INIT(0);

u8 mode = simtemp_mode_normal;
module_param(mode, byte, S_IWUSR | S_IRUGO);

u32 sampling_ms = 100;
module_param(sampling_ms, uint, S_IWUSR | S_IRUGO);

s32 ramp_min = 0;
module_param(ramp_min, int, S_IWUSR | S_IRUGO);

s32 ramp_max = 100000;
module_param(ramp_max, int, S_IWUSR | S_IRUGO);

u32 ramp_period_ms = 1000;
module_param(ramp_period_ms, uint, S_IWUSR | S_IRUGO);

/******************** FUNCTION IMPLEMENTATION ********************/

static void generate_temperature(struct timer_list *timer)
{
        /* ToDo: Get temperature sample from appropiate mode source */
        struct simtemp_sample sample;

        get_temp_sample(&sample);

        ring_buffer_push(&sample);

        atomic_set(&entry_available, 1);
        wake_up_interruptible_sync(&nxp_simtemp_wq);
        (void)mod_timer(&nxp_simtemp_tmr, 
                        jiffies + msecs_to_jiffies(sampling_ms));
}

static int init_timer(void)
{
        timer_setup(&nxp_simtemp_tmr, generate_temperature, 0);
        nxp_simtemp_tmr.expires = jiffies + msecs_to_jiffies(sampling_ms);
        add_timer(&nxp_simtemp_tmr);

        return 0;
}

static void free_timer(void)
{
        timer_shutdown_sync(&nxp_simtemp_tmr);
}

static int nxp_simtemp_open(struct inode *inode, struct file *file)
{
        return 0;
}

static ssize_t nxp_simtemp_read(struct file *file, char __user *out_buff, 
                                size_t req_len, loff_t *loff)
{
        char resp[64];
        size_t size = 0;
        struct simtemp_sample sample;

        if(wait_event_interruptible(nxp_simtemp_wq, atomic_read(&entry_available) == 1))
                return -ERESTARTSYS;

        atomic_set(&entry_available, 0);
        ring_buffer_peek_latest(&sample);
        
        snprintf(resp, 64, "0x%016llx [0x%08x] - %d\n", sample.timestamp, 
                                                      sample.flags,
                                                      sample.temp_mC);

        size = strlen(resp);
        if (size > req_len)
                size = req_len;

        if (copy_to_user(out_buff, resp, size))
                return -EFAULT;

        *loff = size;
        return size;
}

static int nxp_simtemp_release(struct inode *inode, struct file *file)
{
        return 0;
}

static int __init nxp_simtemp_init(void)
{
        int retval;

        cdev_init(&simtemp_dev.cdev, &nxp_simtemp_fops);
        simtemp_dev.cdev.owner = THIS_MODULE;

        retval = alloc_chrdev_region(&simtemp_dev.devnum,
                                     0,
                                     NXP_SIMTEMP_MINOR_COUNT,
                                     NXP_SIMTEMP_DRIVER_NAME);
        if (retval) {
                pr_err("Failed to allocate device numbers");
                goto finish;
        }

        retval = class_register(&nxp_simtemp_class);
        if (retval) {
                pr_err("Failed to create class");
                goto free_chrdev_region;
        }

        retval = init_ring_buffer();
        if (retval) {
                pr_err("Failed to create ring buffer");
                goto unregister_class;
        }

        retval = cdev_add(&simtemp_dev.cdev,
                          simtemp_dev.devnum,
                          NXP_SIMTEMP_MINOR_COUNT);
        if (retval) {
                pr_err("Failed to add char device");
                goto free_ring_buffer;
        }

        simtemp_dev.device = device_create(&nxp_simtemp_class,
                                           NULL,
                                           simtemp_dev.devnum,
                                           NULL,
                                           NXP_SIMTEMP_DEVICE_NAME);
        if (IS_ERR(simtemp_dev.device)) {
                pr_err("Failed to create device");
                retval = -PTR_ERR(simtemp_dev.device);
                goto unregister_cdev;
        }

        retval = init_timer();
        if (retval) {
                pr_err("Failed to create workqueue");
                goto free_device;
        }

        pr_info("Initialized\n");
        return 0;

free_device:
        device_destroy(&nxp_simtemp_class, simtemp_dev.devnum); 
unregister_cdev:
        cdev_del(&simtemp_dev.cdev);
free_ring_buffer:
        destroy_ring_buffer();
unregister_class:
        class_unregister(&nxp_simtemp_class);
free_chrdev_region:
        unregister_chrdev_region(simtemp_dev.devnum, NXP_SIMTEMP_MINOR_COUNT);
finish:
        return retval;
}
module_init(nxp_simtemp_init);

static void __exit nxp_simtemp_exit(void)
{
        free_timer();
        device_destroy(&nxp_simtemp_class, simtemp_dev.devnum);
        cdev_del(&simtemp_dev.cdev);
        destroy_ring_buffer();
        class_unregister(&nxp_simtemp_class);
        unregister_chrdev_region(simtemp_dev.devnum, NXP_SIMTEMP_MINOR_COUNT);
        pr_info("Goodbye!\n");
}
module_exit(nxp_simtemp_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Rafael Martinez <rafael.martinez.r@outlook.com>");
MODULE_DESCRIPTION("NXP simtemp driver");
