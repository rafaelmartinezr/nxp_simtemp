# NXP Simtemp Requirements

## General requirements

- The software shall provide a simulated temperature reading when prompted from user space.

- The software shall be compiled and loaded as an out-of-tree kernel module. Specifically, it shall be a character device.

- The software shall provide a sysfs interface for setting configuration paramters.

- The software shall provide a sysfs interface for getting performance and runtime statistics.

- The software shall provide a threshold alert mechanism.

- Once the module is loaded into the kernel, it shall start simulating readings using the default configuration.

## Temperature readings

- The temperature readings shall be simulated using an appropiate function for the selected `mode`.

- The software shall support 3 separate simulation `mode`s: Normal, noisy and ramp.

- A new temperature reading shall be available every `sampling_ms` milliseconds.

- The software shall be able to store the last `buffer_size` samples.

- A temperature sample shall be provided to the user-space using the following data structure

```c
struct simtemp_sample {
    u64 timestamp;        // timestamp since boot, in ms
    s32 temp_mC;          // temperature in milli-Celsius
    u32 flags;            // Sample flags
}
```

- For simulation purposes, the temperature reading shall be bounded to the range [-50, 120].

### Modes

- For the **normal** mode, the software shall simulate the temperature readings using a smooth noise function (e.g. Perlin noise)

- For the **noisy** mode, the software shall simulate the temperature readings using a PRNG.

- For the **ramp** mode, the software shall simulate the temperature readings using a sawtooth function, which shall be configurable by the parameters: `ramp_max`, `ramp_min`, `ramp_period_ms`

## Reading Policy

### SEEK operation

- The device shall be seekable. This is to allow access to past readings, of course bound by the `buffer_size`.

- When `seek`ing to the start of the device, the device shall set the offset pointer to the oldest entry in the buffer.

- When `seek`ing to the end of the device, the device shall set the offset pointer to the latest entry in the buffer.

- When `seek`ing to anywhere between the start and end of the device, the device shall first check if the requested offset is in the middle of an entry. If so, it shall align the offset to the beginning of said entry.

- `seek`ing beyond the end of the device or before the start of the device shall be rejected with EINVAL.

- The offset pointer shall be relative to the entry in the buffer. In other words, if the entry to which it points gets displaced, the offset shall mantain its relative position. 
    - For example, if the offset points to the third entry, even if the entry gets displaced with new data, once a read call is issued, it shall return the third entry at that point in time.

### READ operation

- Upon opening the device for reading, the offset pointer shall be set to the end of device (i.e. the latest entry).

- Consecutive `read` calls shall yield consecutive entries in the buffer. 

- If the end of the buffer is reached and a `read` call is issued, the call shall block until new data is available.

- If a `read` call prompts multiple entries but the call would block, the call shall return once an entry is available, even if it doesn't yield the entry count requested.

- The device shall never respond with EOF.

- A partial read (i.e. a `read` call that is not aligned to the entry size of the buffer) shall be rejected with EINVAL.

- The device shall support non-blocking reads, in which case, if a `read` call would block, it shall respond with EWOULDBLOCK

## Threshold alert

- The device shall provide a sysfs node named `threshold_mC`, which shall serve for configuring a threshold temperature measured in milli-Celsius

- The device shall provide a sysfs node named `hysteresis_mC`, which shall serve for configuring a hysteresis band for the temperature threshold, also measured in milli-Celsius.

- Once a temperature sample reaches or exceeds the set threshold, this and all future readings shall have the flag `THRESHOLD_CROSSED` set in their `flags` field.

- Once a temperature sample goes under the hysteresis band and the threshold was previously crossed, this and all future readings shall have the flag `THRESHOLD_CROSSED` cleared in their `flags` field.

- The sample that crosses the threshold shall wake up all processes waiting on a `poll` call.

- The sample that goes below the hystereis band shall wake up all processes waiting on a `poll` call.

## Configuration parameters

## Statistics
