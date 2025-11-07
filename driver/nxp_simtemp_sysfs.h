#ifndef NXP_SIMTEMP_SYSFS_H
#define NXP_SIMTEMP_SYSFS_H

#include <linux/types.h>

/* Signal Generator modes */
enum simtemp_generator_mode{
    simtemp_mode_normal,
    simtemp_mode_noisy,
    simtemp_mode_ramp
};

extern enum simtemp_generator_mode mode;
extern u32 sampling_ms;
extern s32 ramp_min;
extern s32 ramp_max;
extern u32 ramp_period_ms;
extern s32 threshold_mC;
extern u32 hysteresis_mC;

extern const struct attribute_group *nxp_simtemp_attr_groups[];

#endif
