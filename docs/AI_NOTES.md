Premise:
I'll be asking you some questions about device driver development for Linux. I've been reading the Linux device drivers third edition book and now I would like the most up-to-date information for the more recent kernels. In your answers, please cite your sources and keep them authoritative.

Q:
Looking at the kernel source I see that a lot of drivers have a core.c file. What should be contained within this file?

Q:
What are the APIs that can be used to interface with the sysfs nodes? In other words, how do I register, read from or write to these nodes from the character device?

Q:
how do i assign permissions to a device node created by device_create?

Q:
im using a workqueue in my kernel device, but i see its imprecise with the delay. how can i increase precision?

Q: 
i have some functions defined in another c file for my device. how do i compile and link it all using kbuild?

Q:
please provide me with a lerp implementation that takes as parameters an s32 start, s32 stop, u32 tmax, u32 t, and returns an s32. tmax is the max value t can take. the min value that t can take is 0.

Q:
now i want a function that returns a sample from a smooth noise generator. my requirements are that it shall not perform any floating point operations, and the result shall be a s32 that is in the range [MIN_TEMP, MAX_TEMP], which both are macros defined elsewehere. this shall be fast, as it executes in softirq context. precomputing of any required configuration is allowed, as this can be stored as a const in my source.

Q:
since the value table can be precomputed, give me the python code to do that
