# NXP Simtemp Device Driver

The main goal of this implementation was to provide a device that could supply as many interesed consumers as there could be, and to provide an easy to use interface which would cover the most common use-case. 

This use-case could be described as so: let's imagine the device is a real sensor, and our system needs to take action depending on the temperature. Maybe we need to run a control algorithm based on the reading, or shutdown the system due to overheating, or raise an alert to the user through an HMI, or all of the above. Here the system could be running multiple processes which would poll the device for the most recent reading of the sensor.

Thus our device should be capable of serving multiple consumers, with minimal to no starvation; provide notification alerts when a certain temperature is reached; and make it easy to get the newest entry available, in fact it should be the default behavior.

## Architecture
![Architectue diagram](./architecture_diagram.svg)

To fulfill our requirements, the following components were implemented :
- **Core**: Is the glue that binds everything together. Responsible of the device's registration with the system. Contains also the fops interface logic and the main producer loop.
- **Ring buffer**: Provides the storage for the samples. 
- **Generators**: Implements the signal generators to simulate the temperature readings. Works as a selector of the configured mode and contains all state information needed for each generator.
- **Sysfs**: Provides the structs for registering sysfs attributes with the system, as well as the store/show function pairs for each attr. Thus, also handles all the validation logic for all the parameters. Should also implement display logic for the statistics when it is available. 

### Core
The core fulfills 3 main purposes:
- Register the device with the system and init the state for the correct operation of the device.
- Implement the timer callback, which serves as the producer loop. This also means, notifying each consumer of our device when new data is available.
- Implement the file operations our device provides.

#### The producer-consumer logic
Each time a process opens our device for reading, we assign to the file pointer a `nxp_simtemp_dev_handle_t` struct. This struct contains 3 fields:
- `latest_available`: Flag used to keep track if the latest entry of the buffer has been read.
- `entry_idx`: Indicated which is the next entry the consumer will read. Effectively, serves as the offset pointer of the file.
- `node`: a list_head node.

Once a new consumer arrives, it registers itself into a consumers list that the core maintains. The purpose of this list is to notify the consumers when new data is ready.

This surely raises the question of whether a wait queue is enough to fulfill this task? 
In this case, no. Since the buffer is designed to be non-destructive (i.e. an entry is not lost upon reading it, hence why the read method is peek and not pop), the latest entry is always available to the consumers. This poses a problem: if a device is only interested in the latest reading and we do not wish to spam it with the same entry each time it calls `read`, how can we know if the latest entry has been read at least by the consumer? We could try and have a flag specific to the entry which is cleared once a consumer reads it, but if we have multiple consumers, this would competition between them which would starve all but one; and would defeat the purpose of a non-destructive buffer.
Thus, the proposed solution is that each consumer should have their own flag to know if they have already read the latest entry.

Once the producer loop has a new entry available, we iterate over our consumers list and set their individual `latest_available` flag to let them know they can read again. Once they consume that entry, they clear their flag. If they try to call `read` immediately after that, they are sent to sleep, as there is no new data until the producer loop ticks again.

### Ring buffer
The ring buffer that has been implemented provides a LIFO interface. This fits well our requirements, as we are mainly interested in the latest entry. None the less, we can peek at any entry with the implemented API.

The main consideration needed here is concurrency.

The ring buffer has been designed to be self-containing. This means: it handles its own logic, including its own locking policy. However, this imposes an important constraint: the `push()` method may only be called from within SoftIRQ context. This is because the structure is internally protected by a rw_spinlock. The `peek()` methods only take the read lock, so they can be called from any context without risk of deadlocks. However, the `push()` method takes the write lock. If `push()` were called from a non-IRQ context and in the middle the ktimer interrupt happened, upon trying to push a new entry, the lock would spin waiting for the other process to release it, and if that process was scheduled within the same CPU, then a deadlock would happen. While we could disable interrupts, `push()` is called mainly from the ktimer callback, so this would negatively impact performance, as this would introduce unpredictable latency to the IRQ. However, this aligns perfectly with our purposes, since the only place were we need to push new entries is from the producer loop.

### Generators
The signal generators are pretty straight-forward. A single interface is exposed here: `get_temp_sample()`. This function works as a selector of the desired generator, calls it and adds the timestamp to the produced sample.

To select the operation mode and the parameters of the generators, the component depends directly upon the attributes defined by sysfs. However, making sure the parameters are valid is a task of their maintainer, which is the sysfs component. Thus, the generators assume that their configuration parameters are always in a valid state, and thus perform no sanity checks to allow for optimization.

It is also important to mention that the threshold handling is not a responsibility of the generators, rather it is of the core. This was decided upon because being in the _alert_ state is a device-wide situation and it is better handled by the core of the device. This also simplifies the alert notification logic.

### Sysfs
This is the most simple component: it is responsible of defining all the structures that the device needs to register with the system in order expose the attributes of our device.

Here each attribute has a pair of `show`/`store` functions. 
The `show()` method simply prints the text representation of our attribute. 
The `store()` method is slightly more complicated: it needs to safely parse the userspace input and validate it according to our requirements. While this is not particularly difficult, care needs to be taken that the raw input is sanitized correctly, or we could compromise the system. Thankfully, the kernel provides parsing functions exactly for this purpose.

Internally to our device, this component only exposes an attributes_group array, which contains all the attributes that are device-wide that userspace can use to control th behavior of our software.

## Locking policies

In this implementation only 2 structures need to be protected: the ring buffer and the consumers list.

The philosphy of this design is that each component shall be responsible of locking their own members. When the core makes a call to any of the functions of the ring buffer, it assumes it will appropiately perform its required locking. In other words, from the POV of each component, any method provided by any other component is atomic. This simplifies the internal logic of each component when making use of any function provided outside of it.

The ring buffer locking policy is described in the ring buffer section.

For the consumers list, since it is never seen from outside the core, there was no point of containing it within is own component. The policy is simple: any access that modifies the list needs to be locked appropiately, disabling interrupts if needed. 

The list is modified mainly at 3 points:
- When a new consumer is registered (a process performs a new `open()` call)
- When a consumer is unregisters (a file descriptor lifetime ends and the kernel calls `release()`)
- When the registered consumers are notified of new data.

Since the list is mainly used for writing, there was no advantage seen of a rw_spinlock over an standard spinlock. Also, the spinlock is the only locking primitive that is valid here, as one of the modification points is inside a SoftIRQ context.

## The user-space interface
The access to the device from userspace is mainly done through the `/dev/simtemp` node and the attribute nodes in `/sys/class/nxp_simtemp/simtemp`.
However, by default these nodes are created with root-only access. Since it is unreasonable to have to have root priviledges each time we want to check the temperature, a udev policy was implemented, which registers the nodes to be accesable for both read and write access to all members of the `simtemp` group. This group is created when the kernel module is installed into a system and adds the current user to the group.

Once the module is loaded, the udev policy is triggered, and the nodes are made accessible immediately.

## Limitations
This design is not without its flaws:

First of all, the consumer list. While for a few processes the performance impact of iterating over the list is minimal, it does not scale well the more consumers are added to the list. This is worsened when considering that iterating over the list happens within the SoftIRQ context, which means other interrupts happening in the same CPU are going to experience a higher latency the more processes are in the list.

This can even be seen when raising the sampling rate to the maximum: `dmesg` will start reporting warnings related to performance metrics of the kernel.

Second, if we wished to use the same driver for multiple devices, this is not possible with the current implementation, and would need major rework to enable this. This is because during the design phase, it was contemplated that only one device would ever be needed. However, in a real scenario where the simulated sensor is replaced by a physical one and we wish have multiple of these, it is easy to see that the current implementation falls short.

Lastly, the maximum sampling rate is 1ms/sample, or 1kHz. If we wished for a higher sampling frequency, first the ktimer would need to be replaced by a hrtimer. Also, the producer method would need to be highly optimized in order for it to complete within the reduced time window of a higher frequency. 
