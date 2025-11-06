#ifndef NXP_SIMTEMP_H
#define NXP_SIMTEMP_H

#include <linux/types.h>

struct simtemp_sample {
    u64 timestamp;        // timestamp since boot, in ms
    u32 temp_mC;          // temperature in milli-Celsius
    u32 flags;            // Sample flags (definition pending)
} __attribute__((packed));

#endif
