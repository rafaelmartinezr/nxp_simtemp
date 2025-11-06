#ifndef NXP_SIMTEMP_GENERATORS
#define NXP_SIMTEMP_GENERATORS

#include "nxp_simtemp.h"

extern u8 mode;
extern u32 sampling_ms;
extern s32 ramp_min;
extern s32 ramp_max;
extern u32 ramp_period_ms;

/* Signal Generator modes */
enum simtemp_generator_mode{
    simtemp_mode_normal,
    simtemp_mode_noisy,
    simtemp_mode_ramp
};

void get_temp_sample(struct simtemp_sample *sample);

#endif
