#ifndef NXP_SIMTEMP_H
#define NXP_SIMTEMP_H

#include <linux/types.h>

#define MIN_TEMP (s32)-50000
#define MAX_TEMP (s32)120000

/* Custom event bits for poll */
#define EPOLLTHRESHCROSSED (u32)0x00100000
c
/* Status flags for the sample */
#define NEW_SAMPLE         0x01
#define THRESHOLD_CROSSED  0x02

struct simtemp_sample {
    u64 timestamp;        // timestamp since boot, in ms
    s32 temp_mC;          // temperature in milli-Celsius
    u32 flags;            // Sample flags
} __attribute__((packed));

#endif
