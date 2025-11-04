# NXP Simtemp Requirements

## General requirements

1. The software shall provide a simulated temperature reading when prompted from user space.

2. The software shall be compiled and loaded as an out-of-tree kernel module. Specifically, it shall be a character device.

3. The software shall provide a sysfs interface for setting configuration paramters.

4. The software shall provide a sysfs interface for getting performance and runtime statistics.

5. The software shall provide a threshold alert mechanism.

## Temperature readings

1. The temperature readings shall be simulated using an appropiate function for the selected `mode`.

2. The software shall support 3 separate simulation `mode`s: Normal, noisy and ramp.

3. A new temperature reading shall be available every `sampling_ms` milliseconds.

4. The software shall be able to store the last `buffer_size` samples.

5. A temperature sample shall be provided to the user-space using the following data structure

```c
struct simtemp_sample {
    u64 timestamp_ns;     // monotonic timestamp
    s32 temp_mC;          // temperature in milli-Celsius
    u32 flags;            // Sample flags
}
```

6. For simulation purposes, the temperature reading shall be bounded to the range [-50, 120].

### Modes

1. For the **normal** mode, the software shall simulate the temperature readings using a smooth noise function (e.g. Perlin noise)

2. For the **noisy** mode, the software shall simulate the temperature readings using a PRNG.

3. For the **ramp** mode, the shall simulate the temperature readings using a periodic ramp (aka sawtooth) function, which shall be configurable by the parameters: `ramp_max`, `ramp_min`, `ramp_period_ms`

## Threshold alert

## Configuration parameters

## Statistics
