import struct
import argparse
import os
import time
import errno
import select

# --- Constants ---
DEVICE_PATH = '/dev/simtemp'
SYSFS_PATH = '/sys/class/nxp_simtemp/simtemp'
# u64 (timestamp), s32 (temp_mC), u32 (flags)
SAMPLE_FORMAT = 'QiI'
SAMPLE_SIZE = struct.calcsize(SAMPLE_FORMAT)  # Should be 16 bytes
THRESHOLD_CROSSED = 0x01  # Sample flag as defined in requirements
THRESHOLD_CROSSED_FLAG = 0x01

def mC_to_C(mC):
    """Converts milli-Celsius to standard Celsius."""
    return mC / 1000.0

def set_sysfs_param(param_name, value):
    """Writes a value to a sysfs parameter file."""
    try:
        path = os.path.join(SYSFS_PATH, param_name)
        with open(path, 'w') as f:
            f.write(str(value))
        print(f"Set {param_name} to {value}")
    except FileNotFoundError:
        print(f"Error: Sysfs path not found: {path}. Is the module loaded?")
    except PermissionError:
        print(f"Error: Permission denied to set {param_name}. Run as root or with correct group permissions.")
    except Exception as e:
        print(f"Error setting {param_name}: {e}")

def get_sysfs_param(param_name):
    """Reads a value from a sysfs parameter file."""
    try:
        path = os.path.join(SYSFS_PATH, param_name)
        with open(path, 'r') as f:
            return f.read().strip()
    except FileNotFoundError:
        return None
    except Exception:
        return None

def configure_device(config_dict):
    """Applies configuration parameters."""
    if not config_dict:
        return

    print("Applying Configuration...")
    for key, value in config_dict.items():
        set_sysfs_param(key, value)
    print("-" * 25)

def user_mode(args):
    """
    Default mode: Reads and prints temperature records continuously
    and handles configuration changes.
    """
    print("\n--- Simtemp User Mode ---")

    # 1. Handle Configuration
    if hasattr(args, 'config') and args.config:
        configure_device(args.config)

    # 2. Continuous Reading
    # Initialize timeout variables
    start_time = time.monotonic()
    # Convert milliseconds to seconds, or set to None if no timeout
    timeout_seconds = args.timeout / 1000.0 if args.timeout is not None else None
    
    # Check current mode/sampling rate for context
    # Removed get_sysfs_param calls for brevity and focus on core task
    
    if timeout_seconds:
        print(f"Running for {timeout_seconds:.3f} seconds before exiting.")
    else:
        print(f"Listening for samples from {DEVICE_PATH}... (Ctrl+C to stop)")
    try:
        # Open device file in binary read mode
        # The requirements state: "Upon opening the device for reading, the offset pointer shall be set to the end of device (i.e. the latest entry)."
        with open(DEVICE_PATH, 'rb') as f:
            while True:
                # Check timeout condition FIRST
                if timeout_seconds is not None and (time.monotonic() - start_time) > timeout_seconds:
                    print("\nTimeout reached. Exiting read mode.")
                    break

                # Read exactly one sample (16 bytes)
                data = f.read(SAMPLE_SIZE)

                if data:
                    # Unpack the binary data
                    timestamp, temp_mC, flags = struct.unpack(SAMPLE_FORMAT, data)

                    temp_C = mC_to_C(temp_mC)
                    alert = "**ALERT**" if (flags & THRESHOLD_CROSSED) else ""

                    print(f"[{(timestamp/10 ** 9):.6f}] | Temp: {temp_C:6.3f} C | Flags: 0x{flags:04x} {alert}")

    except FileNotFoundError:
        print(f"Error: Device file not found: {DEVICE_PATH}. Is the module loaded?")
    except KeyboardInterrupt:
        print("\nReading stopped by user.")
    except Exception as e:
        print(f"Error occurred during reading: {e}")

def test_poll_functionality():
    """
    Tests the poll functionality by setting the device to ramp mode
    and waiting for a threshold crossing event signaled by POLLPRI.
    """
    print("\n--- TEST: Device Poll/Select Functionality ---")
    
    # 1. Setup Configuration for a predictable event
    TEST_THRESHOLD_MC = 10000  # 10 C
    TEST_HYSTERESIS_MC = 5000  # 5 C (Band is 5C to 10C)
    TEST_RAMP_MAX_MC = 30000   # 30 C
    TEST_RAMP_MIN_MC = 11000       # 0 C
    TEST_RAMP_PERIOD_MS = 2000 # Ramp cycle every 2 seconds (1s up, 1s down)
    TEST_SAMPLING_MS = 100     # 10 samples per second

    try:
        print(f"1. Configuring device to trigger event: Ramp mode {TEST_RAMP_MIN_MC}mC to {TEST_RAMP_MAX_MC}mC every 2s).")
        print(f"   Threshold: {TEST_THRESHOLD_MC}mC | Hysteresis: {TEST_HYSTERESIS_MC}mC")
        
        set_sysfs_param('mode', 'ramp')
        set_sysfs_param('threshold_mC', TEST_THRESHOLD_MC)
        set_sysfs_param('hysteresis_mC', TEST_HYSTERESIS_MC)
        set_sysfs_param('ramp_max', TEST_RAMP_MAX_MC)
        set_sysfs_param('ramp_min', TEST_RAMP_MIN_MC)
        set_sysfs_param('ramp_period_ms', TEST_RAMP_PERIOD_MS)
        set_sysfs_param('sampling_ms', TEST_SAMPLING_MS)
        
        print("   Configuration complete. Opening device.")

    except RuntimeError:
        print("   -> FAIL: Failed to configure device via sysfs. Aborting poll test.")
        return

    # 2. Open the device file and initialize poll
    try:
        # Open device in binary read mode
        fd = os.open(DEVICE_PATH, os.O_RDONLY)
        
        poller = select.poll()
        # Register the device file descriptor for read (POLLIN) events
        poller.register(fd, select.POLLIN | select.POLLPRI) 
        
        print("2. Polling device (waiting up to 10 seconds for event)...")
        
        # We expect the event to occur roughly 1000ms after the ramp starts at 0C
        # Use a generous timeout (10 seconds)
        events = poller.poll(10000) 

        if not events:
            print("   -> FAIL: Poll timed out (10s). The device never signaled an event.")
            return

        # 3. Analyze Poll Event
        fileno, event_mask = events[0]
        
        print(f"3. Poll returned event mask: 0x{event_mask:04x}")
        
        # Check for the custom event bit
        if event_mask & select.POLLPRI:
            print(f"   -> PASS: Custom event bit (0x{select.POLLPRI:04x}) was set.")
        else:
            print(f"   -> FAIL: Custom event bit (0x{select.POLLPRI:04x}) was NOT set.")
            print("   (Driver failed to set the urgent flag upon threshold crossing.)")
            # Continue to read to clear the event, but the test fundamentally failed
        
        # Check for POLLIN (standard data availability)
        if event_mask & select.POLLIN:
            print(f"   -> PASS: POLLIN (0x{select.POLLIN:08x}) was set (Data available).")
        else:
            print(f"   -> FAIL: POLLIN was NOT set. (Poll must indicate data available on event.)")
            
        
        # 4. Verify the Read Data
        print("4. Reading sample to verify threshold flag and clear event...")
        
        # Set file descriptor to non-blocking mode temporarily if it wasn't already (os.open used it)
        # We must ensure we get the data that caused the event.
        data = os.read(fd, SAMPLE_SIZE)

        timestamp, temp_mC, flags = struct.unpack(SAMPLE_FORMAT, data)
        temp_C = mC_to_C(temp_mC)
        
        print(f"   Sample Read: Temp={temp_C:.3f}C, Flags=0x{flags:04x}")

        # Check if the sample's internal flag matches the expected cause
        if flags & THRESHOLD_CROSSED_FLAG:
            print(f"   -> PASS: Read sample confirms THRESHOLD_CROSSED (0x{THRESHOLD_CROSSED_FLAG:04x}) flag is set.")
        else:
            print(f"   -> FAIL: Read sample's flag (0x{flags:04x}) did not show THRESHOLD_CROSSED flag.")
            

    except OSError as e:
        if e.errno == errno.ENODEV or e.errno == errno.ENOENT:
             print(f"\n   -> TEST ABORTED: Device file not found: {DEVICE_PATH}. Is the module loaded?")
        else:
            print(f"\n   -> TEST FAILED due to unexpected OSError: {e}")
    except Exception as e:
        print(f"\n   -> TEST FAILED due to unexpected error: {e}")
    finally:
        if 'fd' in locals() and fd >= 0:
            os.close(fd)
            print("   Device file closed.")

def test_threshold_flag():
    print("\n[TEST 3] Threshold Flag Check (requires data generation)")
    
    # Set known parameters
    set_sysfs_param('mode', 'ramp')
    set_sysfs_param('ramp_max', 50000)    # 50 C
    set_sysfs_param('ramp_min', -50000)   # -50 C
    set_sysfs_param('ramp_period_ms', 2000) # 2 seconds to cycle
    set_sysfs_param('threshold_mC', 10000) # 10 C
    set_sysfs_param('hysteresis_mC', 5000) # 5 C

    with open(DEVICE_PATH, 'rb') as f:
        print("   - Reading for 5 seconds to look for THRESHOLD_CROSSED flag...")
        
        start_time = time.time()
        crossed_found = False
        
        # We need to seek to the start to ensure we see the ramp up from ramp_min
        # Note: os.lseek arguments are (fd, offset, whence).
        # Whence=0 (SEEK_SET) seeks from start. The requirement is to seek to the
        # "oldest entry in the buffer". For now, we assume 0 is the start entry index.
        try:
            # os.lseek(f.fileno(), 0 * SAMPLE_SIZE, os.SEEK_SET) # Commented out, buffer size unknown
            pass 
        except Exception:
            # Seeking might fail if driver doesn't implement lseek fully
            pass

        while time.time() - start_time < 5:
            try:
                data = f.read(SAMPLE_SIZE)
                if len(data) == SAMPLE_SIZE:
                    _, temp_mC, flags = struct.unpack(SAMPLE_FORMAT, data)
                    
                    if flags & THRESHOLD_CROSSED:
                        crossed_found = True
                        print(f"   - PASS: THRESHOLD_CROSSED flag found at {mC_to_C(temp_mC):.2f} C.")
                        break
                time.sleep(0.01) # Small delay to not busy-wait too aggressively
            except BlockingIOError:
                time.sleep(0.01)
            except Exception as e:
                print(f"   - Error during threshold read loop: {e}")
                break

        status = "PASS" if crossed_found else "FAIL (Threshold not crossed within 5s or flag not set)"
        print(f"   - Threshold Flag Check: {status}")

def test_read():
    print("\n[TEST 2] Basic read operation (one sample)")
    try:
        with open(DEVICE_PATH, 'rb') as f:
            # Read one sample
            data = f.read(SAMPLE_SIZE)
            if len(data) == SAMPLE_SIZE:
                timestamp, temp_mC, flags = struct.unpack(SAMPLE_FORMAT, data)
                print(f"   - PASS: Read 1 sample ({SAMPLE_SIZE} bytes) successfully. Temp: {mC_to_C(temp_mC):.2f} C")
            else:
                print(f"   - FAIL: Read returned {len(data)} bytes, expected {SAMPLE_SIZE}.")

    except FileNotFoundError:
        print(f"   - FAIL: Device file not found: {DEVICE_PATH}")
    except Exception as e:
        print(f"   - FAIL: Test 2 failed with exception: {e}")

def test_set_mode():
    def check_mode(expected):
        """Helper to verify current mode."""
        current_mode = get_sysfs_param('mode')
        success = (current_mode == expected)
        status = "PASS" if success else f"FAIL (Got: {current_mode})"
        print(f"   - Mode verification: Expected '{expected}'. {status}")
        return success

    # --- Test 1: Configuration via sysfs ---
    print("\n[TEST 1] Configuration via sysfs")
    try:
        print("   - Setting device to 'ramp' mode...")
        set_sysfs_param('mode', 'ramp')
        check_mode('ramp')
        
        # Test ramp parameters
        print("   - Setting ramp parameters (100C max, -40C min, 5s period)...")
        set_sysfs_param('ramp_max', 100000)
        set_sysfs_param('ramp_min', -40000)
        set_sysfs_param('ramp_period_ms', 5000)
        
        # Verify a setting
        ramp_max = get_sysfs_param('ramp_max')
        status = "PASS" if ramp_max == '100000' else f"FAIL (Got {ramp_max})"
        print(f"   - ramp_max check: {status}")

    except Exception as e:
        print(f"   - FAIL: Test 1 failed with exception: {e}")

def test_mode(args):
    """Runs a series of tests to verify device functionality."""
    print("\n--- Simtemp Test Mode ---")

    test_set_mode()
    test_read()
    test_threshold_flag()
    test_poll_functionality()

def main():
    parser = argparse.ArgumentParser(
        description="NXP Simtemp Device Driver Interaction Script.",
        formatter_class=argparse.RawTextHelpFormatter
    )

    # --- User Mode Subparser (Default) ---
    parser.add_argument(
        '-c', '--config',
        nargs='+',
        action='store',
        metavar='KEY=VALUE',
        help="""Configure device parameters via sysfs.
Examples:
  --config mode=ramp ramp_max=110000
  --config threshold_mC=50000 sampling_ms=500
"""
    )
    parser.add_argument(
        '-t', '--timeout',
        type=int,
        metavar='MS',
        help='Run in user mode for the specified duration (in milliseconds) and then exit.'
    )

    # Subparsers for modes
    subparsers = parser.add_subparsers(dest='mode', required=False, help='Operation mode')
    subparsers.add_parser('test', help='Run a set of functional tests.')

    args = parser.parse_args()

    # Process config arguments into a dictionary
    config_dict = {}
    if args.config:
        for item in args.config:
            if '=' in item:
                key, value = item.split('=', 1)
                config_dict[key] = value
            else:
                print(f"Warning: Invalid config format '{item}'. Use KEY=VALUE.")
        args.config = config_dict
    
    # Execution logic:
    # If 'test' is provided, enter test mode.
    # Otherwise (args.mode is None or another command which we now treat as default), enter user mode.
    if args.mode == 'test':
        test_mode(args)
    else:
        # Default behavior: assume we are in read mode
        args.read = True 
        user_mode(args)

if __name__ == '__main__':
    main()
