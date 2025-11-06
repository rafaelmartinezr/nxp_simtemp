#ifndef NXP_SIMTEMP_H
#define NXP_SIMTEMP_H

#include <linux/types.h>

#define MIN_TEMP (s32)-50000
#define MAX_TEMP (s32)120000

struct simtemp_sample {
    u64 timestamp;        // timestamp since boot, in ms
    s32 temp_mC;          // temperature in milli-Celsius
    u32 flags;            // Sample flags (definition pending)
} __attribute__((packed));

#endif
