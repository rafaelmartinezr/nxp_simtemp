#!/bin/bash

# --- Configuration ---
DRIVER_PATH="../driver"

# --- 1. Save Original Directory ---
# Use pushd to save the current working directory to the stack.
# We suppress the output (> /dev/null) for cleaner script execution.
pushd . > /dev/null

# --- 2. Determine Command ---
# Check if a parameter ($1) was provided. If not, default the COMMAND to "build".
COMMAND=${1:-"build"}

# --- 3. Execute Action based on Command ---
case "$COMMAND" in
    build)
        # Validate the parameter is one of the allowed options.
        # Action: Navigate to DRIVER_PATH and run 'make'.
        if [ -d "$DRIVER_PATH" ]; then
            echo "Navigating to $DRIVER_PATH and running 'make'..."
            cd "$DRIVER_PATH" && make
            # $? captures the exit status of the last command (make).
            if [ $? -ne 0 ]; then
                echo "Warning: 'make' command failed."
            fi
        else
            echo "Error: Directory '$DRIVER_PATH' not found. Cannot perform build."
        fi
        ;;

    clean)
        # Action: Navigate to DRIVER_PATH and run 'make clean'.
        if [ -d "$DRIVER_PATH" ]; then
            echo "Navigating to $DRIVER_PATH and running 'make clean'..."
            cd "$DRIVER_PATH" && make clean
            if [ $? -ne 0 ]; then
                echo "Warning: 'make clean' command failed."
            fi
        else
            echo "Error: Directory '$DRIVER_PATH' not found. Cannot perform clean."
        fi
        ;;

    install) 
        # Check for root/sudo privileges ($EUID is 0 for root)
        if [ "$EUID" -ne 0 ]; then
            echo "Error: The '$COMMAND' action requires root privileges. Please run this script with 'sudo'."
            # Clean up directory stack and exit with an error status
            popd > /dev/null
            exit 1
        fi

        # # Navigate to DRIVER_PATH and run 'make install'.
        cd "$DRIVER_PATH" && sudo make install
        if [ $? -ne 0 ]; then
            echo "Warning: 'sudo make install' command failed."
        fi
        sudo depmod -a
        
        # Create simtemp users group
        echo "Creating user group..."
        sudo groupadd -f simtemp
        USER=$(logname 2>/dev/null)
        sudo usermod -aG simtemp $USER

        echo "Installing and reloading udev rules..."
        sudo cp 99-simtemp.rules /etc/udev/rules.d/99-simtemp.rules
        sudo udevadm control --reload-rules

        echo "Please login again to reload group permissions"
        ;;

    uninstall)
        # Check for root/sudo privileges ($EUID is 0 for root)
        if [ "$EUID" -ne 0 ]; then
            echo "Error: The '$COMMAND' action requires root privileges. Please run this script with 'sudo'."
            # Clean up directory stack and exit with an error status
            popd > /dev/null
            exit 1
        fi

        # Delete ko from modules path, delete rules and delete group
        sudo rmmod nxp_simtemp 2>/dev/null
        sudo rm /lib/modules/$(uname -r)/updates/nxp_simtemp.ko
        sudo rm /etc/udev/rules.d/99-simtemp.rules 
        sudo groupdel simtemp
        echo "Please login again to reload group permissions!"
        ;;

    *)
        # Handle invalid parameters.
        echo "Invalid parameter: '$COMMAND'."
        echo "Usage: $0 [build|install|uninstall|clean]"
        # Exit with an error status code
        exit 1
        ;;
esac

# --- 4. Return to Original Directory ---
# Use popd to return to the directory saved by pushd.
popd > /dev/null

# Exit with the status of the last executed command
exit 0
