
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
#include <linux/workqueue.h>
#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/ktime.h>

/******************** DATA TYPES ********************/

typedef struct nxp_simtemp_device
{
        dev_t devnum;
        struct device *device;
        struct cdev cdev;
        struct workqueue_struct *workqueue; 
} nxp_simtemp_dev_t;

/******************** FUNCTION PROTOTYPES ********************/

static int nxp_simtemp_open(struct inode *inode, struct file *file);
static ssize_t nxp_simtemp_read(struct file *file, char __user *out_buff, 
                                size_t req_len, loff_t *loff);
static int nxp_simtemp_release(struct inode *inode, struct file *file);

static void generate_temperature(struct work_struct* data);

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

static DECLARE_WAIT_QUEUE_HEAD(nxp_simtemp_wq);
static DECLARE_DELAYED_WORK(nxp_simtemp_worker_gen_temp, generate_temperature);

atomic_t flag = ATOMIC_INIT(0);

static uint sampling_ms = 1000;
module_param(sampling_ms, uint, S_IWUSR | S_IRUGO);

/******************** FUNCTION IMPLEMENTATION ********************/

static int init_workqueue(const char *name)
{
        int retval = 0;

        simtemp_dev.workqueue = alloc_workqueue(name, 0, 0);
        if (IS_ERR_OR_NULL(simtemp_dev.workqueue)) {
                retval = -ENOMEM;
                goto finish;
        }

        (void)queue_delayed_work(simtemp_dev.workqueue, 
                                &nxp_simtemp_worker_gen_temp, 
                                msecs_to_jiffies(sampling_ms));

finish:
        return retval;
}

static void free_workqueue(void)
{
        cancel_delayed_work_sync(&nxp_simtemp_worker_gen_temp);
        destroy_workqueue(simtemp_dev.workqueue);
}

static void generate_temperature(struct work_struct* data)
{
        atomic_set(&flag, 1);
        wake_up_interruptible_sync(&nxp_simtemp_wq);
        (void)queue_delayed_work(simtemp_dev.workqueue, 
                                &nxp_simtemp_worker_gen_temp, 
                                msecs_to_jiffies(sampling_ms));
}

static int nxp_simtemp_open(struct inode *inode, struct file *file)
{
        return 0;
}

static ssize_t nxp_simtemp_read(struct file *file, char __user *out_buff, 
                                size_t req_len, loff_t *loff)
{
        char resp[32];
        size_t size = 0;
        static ktime_t curr_time = 0, past_time = 0, delta_time;
        struct timespec64 ts;

        if(wait_event_interruptible(nxp_simtemp_wq, atomic_read(&flag) == 1))
                return -ERESTARTSYS;

        atomic_set(&flag, 0);

        past_time = curr_time;
        curr_time = ktime_get();
        delta_time = ktime_sub(curr_time, past_time);
        ts = ktime_to_timespec64(delta_time);
        
        snprintf(resp, 32, "%lld.%.9ld\n", ts.tv_sec, ts.tv_nsec);

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

        retval = cdev_add(&simtemp_dev.cdev,
                          simtemp_dev.devnum,
                          NXP_SIMTEMP_MINOR_COUNT);
        if (retval) {
                pr_err("Failed to add char device");
                goto unregister_class;
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

        retval = init_workqueue("temp_generator");
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
        free_workqueue();
        device_destroy(&nxp_simtemp_class, simtemp_dev.devnum);
        cdev_del(&simtemp_dev.cdev);
        class_unregister(&nxp_simtemp_class);
        unregister_chrdev_region(simtemp_dev.devnum, NXP_SIMTEMP_MINOR_COUNT);
        pr_info("Goodbye!\n");
}
module_exit(nxp_simtemp_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Rafael Martinez <rafael.martinez.r@outlook.com>");
MODULE_DESCRIPTION("NXP simtemp driver");
