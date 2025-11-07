#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include "nxp_simtemp.h"
#include "nxp_simtemp_sysfs.h"

/* Attributes writeable by group and owner, readable by all */
#define ATTR_PERM_RW_POLICY (S_IRUGO | S_IWUSR | S_IWGRP)

#define RAMP_PERIOD_MIN  1
#define RAMP_PERIOD_MAX  UINT_MAX
#define SAMPLING_RATE_MIN  1
#define SAMPLING_RATE_MAX  UINT_MAX

/* Must be in the same order as enum simtemp_generator_mode */
const char* mode_strings[] = {
        "normal",
        "noisy",
        "ramp"
};

enum simtemp_generator_mode mode = simtemp_mode_normal;
u32 sampling_ms = 100;
s32 ramp_min = 0;
s32 ramp_max = 100000;
u32 ramp_period_ms = 1000;
s32 threshold_mC = 50000;
u32 hysteresis_mC = 10000;

ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf);
ssize_t mode_store(struct device *dev, struct device_attribute *attr,
                        const char *buf, size_t count);
DEVICE_ATTR(mode, ATTR_PERM_RW_POLICY, mode_show, mode_store);

ssize_t sampling_ms_show(struct device *dev, struct device_attribute *attr,
        char *buf);
ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count);
DEVICE_ATTR(sampling_ms, ATTR_PERM_RW_POLICY, sampling_ms_show, 
        sampling_ms_store);

ssize_t ramp_min_show(struct device *dev, struct device_attribute *attr,
        char *buf);
ssize_t ramp_min_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count);
DEVICE_ATTR(ramp_min, ATTR_PERM_RW_POLICY, ramp_min_show, ramp_min_store);

ssize_t ramp_max_show(struct device *dev, struct device_attribute *attr,
        char *buf);
ssize_t ramp_max_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count);
DEVICE_ATTR(ramp_max, ATTR_PERM_RW_POLICY, ramp_max_show, ramp_max_store);

ssize_t ramp_period_ms_show(struct device *dev, struct device_attribute *attr,
        char *buf);
ssize_t ramp_period_ms_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count);
DEVICE_ATTR(ramp_period_ms, ATTR_PERM_RW_POLICY, ramp_period_ms_show, 
        ramp_period_ms_store);

ssize_t threshold_mC_show(struct device *dev, struct device_attribute *attr,
        char *buf);
ssize_t threshold_mC_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count);
DEVICE_ATTR(threshold_mC, ATTR_PERM_RW_POLICY, threshold_mC_show,
        threshold_mC_store);

ssize_t hysteresis_mC_show(struct device *dev, struct device_attribute *attr, 
        char *buf);
ssize_t hysteresis_mC_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count);
DEVICE_ATTR(hysteresis_mC, ATTR_PERM_RW_POLICY, hysteresis_mC_show,
        hysteresis_mC_store);

static struct attribute *nxp_simtemp_attrs[] = {
        &dev_attr_mode.attr,
        &dev_attr_sampling_ms.attr,
        &dev_attr_ramp_min.attr,
        &dev_attr_ramp_max.attr,
        &dev_attr_ramp_period_ms.attr,
        &dev_attr_threshold_mC.attr,
        &dev_attr_hysteresis_mC.attr,
        NULL,
};

static const struct attribute_group nxp_simtemp_attr_group = {
        .attrs = nxp_simtemp_attrs,
};

const struct attribute_group *nxp_simtemp_attr_groups[] = {
        &nxp_simtemp_attr_group, 
        NULL
};

/************************* IMPLEMENTATION *************************/

ssize_t mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
        return sysfs_emit(buf, "%s\n", mode_strings[mode]);
}

ssize_t mode_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
        int retval;

        retval =  sysfs_match_string(mode_strings, buf);
        if (retval < 0)
                return retval;
        
        mode = retval;
        return count;
}

ssize_t sampling_ms_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
        return sysfs_emit(buf, "%d\n", sampling_ms);
}

ssize_t sampling_ms_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
        uint input;
        int retval;

        retval = kstrtouint(buf, 0, &input);
        if (retval) 
                return retval;

        if ((input < SAMPLING_RATE_MIN) || (input > SAMPLING_RATE_MAX))
                return -ERANGE;
        
        sampling_ms = input;
        return count;
}

ssize_t ramp_min_show(struct device *dev, struct device_attribute *attr, 
        char *buf)
{
        return sysfs_emit(buf, "%d\n", ramp_min);
}

ssize_t ramp_min_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
        int retval;
        int input;

        retval = kstrtoint(buf, 0, &input);
        if (retval)
                return retval;

        if ((input < MIN_TEMP) || (input > MAX_TEMP))
                return -ERANGE;

        if (input > ramp_max)
                return -EINVAL;
        
        ramp_min = input;
        return count;
}

ssize_t ramp_max_show(struct device *dev, struct device_attribute *attr,
        char *buf)
{
        return sysfs_emit(buf, "%d\n", ramp_max);
}

ssize_t ramp_max_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
        int retval;
        int input;

        retval = kstrtoint(buf, 0, &input);
        if (retval)
                return retval;

        if ((input < MIN_TEMP) || (input > MAX_TEMP))
                return -ERANGE;

        if (input < ramp_min)
                return -EINVAL;
        
        ramp_max = input;
        return count;
}

ssize_t ramp_period_ms_show(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        return sysfs_emit(buf, "%d\n", ramp_period_ms);        
}

ssize_t ramp_period_ms_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{ 
        uint input;
        int retval;

        retval = kstrtouint(buf, 0, &input);
        if (retval) 
                return retval;

        if ((input < RAMP_PERIOD_MIN) || (input > RAMP_PERIOD_MAX))
                return -ERANGE;
        
        ramp_period_ms = input;
        return count;
}

ssize_t threshold_mC_show(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
        return sysfs_emit(buf, "%d\n", threshold_mC);
}

ssize_t threshold_mC_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
        int retval;
        int input;
        int hys_band;

        retval = kstrtoint(buf, 0, &input);
        if (retval)
                return retval;

        if ((input < MIN_TEMP) || (input > MAX_TEMP))
                return -ERANGE;

        hys_band = input - hysteresis_mC;
        if ((hys_band < MIN_TEMP) || (hys_band > MAX_TEMP))
                return -EINVAL;

        threshold_mC = input;
        return count;
}

ssize_t hysteresis_mC_show(struct device *dev, struct device_attribute *attr, 
                        char *buf)
{
        return sysfs_emit(buf, "%d\n", hysteresis_mC);
}

ssize_t hysteresis_mC_store(struct device *dev, struct device_attribute *attr,
        const char *buf, size_t count)
{
        int retval;
        uint input;
        int hys_band;

        retval = kstrtouint(buf, 0, &input);
        if (retval)
                return retval;

        if (input > (MAX_TEMP - MIN_TEMP))
                return -ERANGE;

        hys_band = threshold_mC - input;
        if ((hys_band < MIN_TEMP) || (hys_band > MAX_TEMP))
                return -EINVAL;

        hysteresis_mC = input;
        return count;
}
