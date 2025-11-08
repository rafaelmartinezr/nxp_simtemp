#!/bin/bash

sudo modprobe nxp_simtemp
echo "5s of ramp @ 10ms/sample"
python3 ../user/cli/simtemp.py -t 5000 -c mode=ramp sampling_ms=10
echo "5s of noisy @200ms/delay"
python3 ../user/cli/simtemp.py -t 5000 -c mode=noisy sampling_ms=200
echo "5s of normal@500ms/delay"
python3 ../user/cli/simtemp.py -t 5000 -c mode=noisy sampling_ms=500
sudo rmmod nxp_simtemp
