#!/bin/bash

# Write test
fio --name=fff --ioengine=io_uring --iodepth=16 --rw=write --size=100M --verify_state_save=1 --bssplit=8k/100 --direct=1 --filename=/dev/lsvbd1 --numjobs=1 --verify=pattern --verify_pattern=0xAA --do_verify=0 --verify_fatal=0 --verify_only=0 --runtime=20

# Read and verify test
fio --name=fff --ioengine=io_uring --iodepth=16 --rw=read --size=100M --verify_state_save=1 --bssplit=8k/100 --direct=1 --filename=/dev/lsvbd1 --numjobs=1 --verify=pattern --verify_pattern=0xAA --do_verify=0 --verify_fatal=0 --verify_only=1 --runtime=20
