#!/bin/bash

grep -E 'throttle_status: 0x(?!0{8})|indep_throttle_status: 0x(?!0{16})' gpu_throttling_output.txt
if [ $? -eq 0 ]; then
    echo "Throttling status change detected during the run."
else
    echo "No throttling status change detected during the run."
fi

grep -E 'throttle_status reasons: (?!none)|indep_throttle_status reasons: (?!none)' gpu_throttling_output.txt
if [ $? -eq 0 ]; then
    echo "Throttling reasons detected during the run."
else
    echo "No throttling reasons detected during the run."
fi
