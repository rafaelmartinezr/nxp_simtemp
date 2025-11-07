
/******************** MACROS ********************/

#define NXP_SIMTEMP_MINOR_COUNT 1
#define NXP_SIMTEMP_DRIVER_NAME "nxp_simtemp"
#define NXP_SIMTEMP_CLASS_NAME "nxp_simtemp"
#define NXP_SIMTEMP_DEVICE_NAME "simtemp"

#define pr_fmt(fmt) NXP_SIMTEMP_DRIVER_NAME ": " fmt

#define SAMPLE_BUFFER_SIZE   10

/******************** INCLUDES ********************/

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/atomic.h>
#include <linux/wait.h>
#include <linux/ktime.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#include "nxp_simtemp.h"
#include "nxp_simtemp_buffer.h"
#include "nxp_simtemp_sysfs.h"
#include "nxp_simtemp_generators.h"

/******************** DATA TYPES ********************/

/**
 * Struct containing the objects and state pertaining to the device 
 */
typedef struct nxp_simtemp_device {
        dev_t devnum;          /* Device number range */
        struct device *device; /* Device instance in /dev */
        struct cdev cdev;      /* The char device struct for fops */
        bool in_threshold;     /* Temp threshold state */
        spinlock_t consumers_lock; /* Consumer list lock */
        struct list_head consumers; /* Consumer list */
} nxp_simtemp_dev_t;

/**
 * Struct for each process that interacts with the device
 */
typedef struct nxp_simtemp_dev_handle{
        atomic_t latest_available;
        u32 entry_idx; /* Index of ring buffer entry */
        struct list_head node; /* Consumer node for the consumers list */ 
} nxp_simtemp_dev_handle_t;

/******************** FUNCTION PROTOTYPES ********************/
static int nxp_simtemp_probe(struct platform_device *pdev);
static void nxp_simtemp_remove_new(struct platform_device *pdev);

static int nxp_simtemp_open(struct inode *inode, struct file *file);
static ssize_t nxp_simtemp_read(struct file *file, char __user *out_buff, 
                                size_t req_len, loff_t *loff);
static int nxp_simtemp_release(struct inode *inode, struct file *file);

static bool validate_threshold(struct simtemp_sample *sample);
static void generate_temperature(struct timer_list *data);

/******************** PUBLIC CONST ********************/

/******************** PRIVATE CONST ********************/

static const struct class nxp_simtemp_class = {
        .name = NXP_SIMTEMP_CLASS_NAME,
        .dev_groups = nxp_simtemp_attr_groups,
};

static const struct platform_device_id nxp_simtemp_id[] = {
        {
                .name = NXP_SIMTEMP_DEVICE_NAME,
                .driver_data = 0
        },
        { /* NULL */ },
};

static struct platform_driver nxp_simtemp_driver = {
        .driver = {
                .name = NXP_SIMTEMP_DRIVER_NAME,
                .owner = THIS_MODULE,
        },
        .id_table = nxp_simtemp_id, /* ToDo: change to of_match_table once DT is available */
        .probe = nxp_simtemp_probe,
        .remove_new = nxp_simtemp_remove_new,
};

static const struct file_operations nxp_simtemp_fops = {
    .owner = THIS_MODULE,
    .open = nxp_simtemp_open,
    .read = nxp_simtemp_read,
    .release = nxp_simtemp_release,
};

/******************** PUBLIC VARIABLES ********************/

/******************** STATIC VARIABLES ********************/

static struct platform_device *simtemp_pdev;
static nxp_simtemp_dev_t simtemp_dev;

static struct timer_list nxp_simtemp_tmr;
static DECLARE_WAIT_QUEUE_HEAD(nxp_simtemp_wq);

/******************** FUNCTION IMPLEMENTATION ********************/

/**
 * Validate if the sample has crossed or cleared the temperature threshold
 * @param[in,out]  sample - Sample to validate, might have its THRESHOLD_CROSSED
 *                          modified, depending on the conditions
 * @return bool - True if threshold has been crossed, False otherwise or if
 *                hysteresis band has been cleared
 */
static bool validate_threshold(struct simtemp_sample *sample)
{
        bool retval = false;

        if (sample->temp_mC >= threshold_mC) 
                simtemp_dev.in_threshold = true;
        
        if (simtemp_dev.in_threshold) {
                if (sample->temp_mC <= (threshold_mC - (s32)hysteresis_mC)) {
                        sample->flags &= ~THRESHOLD_CROSSED;
                        simtemp_dev.in_threshold = false;
                } else {
                        sample->flags |= THRESHOLD_CROSSED;
                        retval = true;
                }
        }

        return retval;
}

/**
 * Callback for the ktimer. 
 * Gets a sample from the active generator and notifies consumers of new data.
 */
static void generate_temperature(struct timer_list *timer)
{
        struct simtemp_sample sample;
        nxp_simtemp_dev_handle_t* consumer;

        /* Get the newest sample */
        get_temp_sample(&sample);
        (void)validate_threshold(&sample);
        ring_buffer_push(&sample);

        /* Notify consumers that new data is available */
        spin_lock(&simtemp_dev.consumers_lock);
        list_for_each_entry(consumer, &simtemp_dev.consumers, node){
                atomic_set(&consumer->latest_available, 1);
        }
        spin_unlock(&simtemp_dev.consumers_lock);

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

/**
 * Check if the requested entry is available from the ring buffer
 * @param dev_handle[in] Consumer specific handle
 * @return bool - True if data can be read, false otherwise
 */
static bool check_data_available(const nxp_simtemp_dev_handle_t *dev_handle)
{
        /* Any entry other than the latest is always available */
        bool retval = true;

        /* if the idx is for the latest entry, we need to check the
         * availability flag */
        if (UINT_MAX == dev_handle->entry_idx) 
                retval = atomic_read(&dev_handle->latest_available);

        return retval;
}

static int nxp_simtemp_open(struct inode *inode, struct file *file)
{
        /* Check if module is not being unloaded */
        if (!try_module_get(THIS_MODULE))
                return -ENODEV;

        /* Create the device handle and add process to the list of consumers */
        struct nxp_simtemp_dev_handle *dev_handle = 
                kzalloc(sizeof(struct nxp_simtemp_dev_handle), GFP_KERNEL);
        if (!dev_handle)
                return -ENOMEM;

        /* Our open policy is that it accesses the end of the ring buffer
         * (aka the latest entry). UINT_MAX will be used to symbolize this */
        atomic_set(&dev_handle->latest_available, 0); 
        dev_handle->entry_idx = UINT_MAX;
        
        /* Add process to the consumers list */
        spin_lock_bh(&simtemp_dev.consumers_lock);
        list_add_tail(&dev_handle->node, &simtemp_dev.consumers);
        spin_unlock_bh(&simtemp_dev.consumers_lock);

        /* Finally, add ourselves to the file pointer */
        file->private_data = (void *)dev_handle;

        return 0;
}

static ssize_t nxp_simtemp_read(struct file *file, char __user *out_buff, 
                                size_t req_len, loff_t *loff)
{
        struct simtemp_sample sample_buffer[SAMPLE_BUFFER_SIZE];
        size_t count = 0;
        size_t available_samples;

        nxp_simtemp_dev_handle_t *dev_handle = 
                (nxp_simtemp_dev_handle_t *)file->private_data;

        while (!check_data_available(dev_handle)) {
                if(file->f_flags & O_NONBLOCK)
                        return -EAGAIN;

                if (wait_event_interruptible(nxp_simtemp_wq, check_data_available(dev_handle)))
                        return -ERESTARTSYS;
        }

        /* Check how much of the request, if any, can be supplied */
        count = req_len / sizeof(struct simtemp_sample);
        if (0 == count) {
                /* Request is a partial read, reject */
                return -EINVAL;
        }

        /* Check if latest was requested */
        if (UINT_MAX == dev_handle->entry_idx) {
                /* Clear the availabity flag, as entry is consumed */
                ring_buffer_peek_latest(&sample_buffer[0]);
                atomic_set(&dev_handle->latest_available, 0);
                count = 1;
        } else {
                /* Limit requested samples to the available ones */
                available_samples = get_ring_buffer_size() - dev_handle->entry_idx;
                if (count > available_samples)
                        count = available_samples;

                /* Limit to capacity of write buffer */
                if (count > SAMPLE_BUFFER_SIZE)
                        count = SAMPLE_BUFFER_SIZE;

                /* Read all requested samples into the write buffer */
                for (size_t buff_idx = 0; buff_idx < count; buff_idx++) {
                        ring_buffer_peek(dev_handle->entry_idx, 
                                        &sample_buffer[buff_idx]);
                        dev_handle->entry_idx++;
                }

                /* If the end of the ring buffer was reached, latch to the 
                *  latest entry */
               if (dev_handle->entry_idx == (get_ring_buffer_size()-1))
                        dev_handle->entry_idx = UINT_MAX;
        }

        if (copy_to_user(out_buff, sample_buffer, count * sizeof(struct simtemp_sample)))
                return -EFAULT;

        *loff = dev_handle->entry_idx * sizeof(struct simtemp_sample);
        return count * sizeof(struct simtemp_sample);
}

static int nxp_simtemp_release(struct inode *inode, struct file *file)
{
        struct nxp_simtemp_dev_handle *dev_handle = 
                        (struct nxp_simtemp_dev_handle *)file->private_data;
        
        /* Remove process from consumer list */
        spin_lock_bh(&simtemp_dev.consumers_lock);
        list_del(&dev_handle->node);
        spin_unlock_bh(&simtemp_dev.consumers_lock);

        kfree(dev_handle);

        module_put(THIS_MODULE);
        pr_info("Handle freed\n");

        return 0;
}

static int nxp_simtemp_probe(struct platform_device *pdev)
{
        int retval;

        /* First init all static fields of the device struct */
        simtemp_dev.in_threshold = false;
        spin_lock_init(&simtemp_dev.consumers_lock);
        INIT_LIST_HEAD(&simtemp_dev.consumers);

        /* Init all dynamic elements of the device struct */
        cdev_init(&simtemp_dev.cdev, &nxp_simtemp_fops);
        simtemp_dev.cdev.owner = THIS_MODULE;

        /* Get available device number */
        retval = alloc_chrdev_region(&simtemp_dev.devnum,
                                     0,
                                     NXP_SIMTEMP_MINOR_COUNT,
                                     NXP_SIMTEMP_DRIVER_NAME);
        if (retval) {
                pr_err("Failed to allocate device numbers\n");
                goto finish;
        }

        /* Ring buffer needs to be available before cdev is exposed */
        retval = init_ring_buffer();
        if (retval) {
                pr_err("Failed to create ring buffer\n");
                goto free_chrdev_region;
        }

        /* Expose char device to the system */
        retval = cdev_add(&simtemp_dev.cdev,
                          simtemp_dev.devnum,
                          NXP_SIMTEMP_MINOR_COUNT);
        if (retval) {
                pr_err("Failed to add char device\n");
                goto free_ring_buffer;
        }

        /* Create a /dev node */
        simtemp_dev.device = device_create(&nxp_simtemp_class,
                                           &pdev->dev,
                                           simtemp_dev.devnum,
                                           NULL,
                                           NXP_SIMTEMP_DEVICE_NAME);
        if (IS_ERR(simtemp_dev.device)) {
                pr_err("Failed to create device\n");
                retval = -PTR_ERR(simtemp_dev.device);
                goto unregister_cdev;
        }

        /* Init producer after everything is in place */
        retval = init_timer();
        if (retval) {
                pr_err("Failed to create workqueue\n");
                goto free_device;
        }

        pr_info("Probe success!\n");
        return 0;

free_device:
        device_destroy(&nxp_simtemp_class, simtemp_dev.devnum); 
unregister_cdev:
        cdev_del(&simtemp_dev.cdev);
free_ring_buffer:
        destroy_ring_buffer();
free_chrdev_region:
        unregister_chrdev_region(simtemp_dev.devnum, NXP_SIMTEMP_MINOR_COUNT);
finish:
        return retval;
}

static void nxp_simtemp_remove_new(struct platform_device *pdev)
{
        /* First cancel the producer */
        free_timer();
        device_destroy(&nxp_simtemp_class, simtemp_dev.devnum);
        cdev_del(&simtemp_dev.cdev);
        /* Now that nobody needs to use the buffer, free it */
        destroy_ring_buffer();
        unregister_chrdev_region(simtemp_dev.devnum, NXP_SIMTEMP_MINOR_COUNT);
        pr_info("Device removed\n");
}

static int __init nxp_simtemp_init(void)
{
        int retval;

        retval = class_register(&nxp_simtemp_class);
        if (retval) {
                pr_err("Failed to create class\n");
                goto finish;
        }

        retval = platform_driver_register(&nxp_simtemp_driver);
        if (retval) {
                pr_err("Failed to register driver\n");
                goto unregister_class;
        }

        simtemp_pdev = platform_device_register_simple(NXP_SIMTEMP_DEVICE_NAME,
                                                       -1,
                                                       NULL,
                                                       0);
        if (IS_ERR(simtemp_pdev)) {
                pr_err("Failure registering device\n");
                goto unregister_driver;
        }

        pr_info("Module loaded successfully!\n");
        return 0;

unregister_driver:
        platform_driver_unregister(&nxp_simtemp_driver);
unregister_class:
        class_unregister(&nxp_simtemp_class);
finish:
        return retval;
}
module_init(nxp_simtemp_init);

static void __exit nxp_simtemp_exit(void)
{
        platform_device_unregister(simtemp_pdev);
        platform_driver_unregister(&nxp_simtemp_driver);
        class_unregister(&nxp_simtemp_class);
        pr_info("Goodbye!\n");
}
module_exit(nxp_simtemp_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Rafael Martinez <rafael.martinez.r@outlook.com>");
MODULE_DESCRIPTION("NXP simtemp driver");
