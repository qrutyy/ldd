#!/bin/bash

fio --name=fff --ioengine=io_uring --iodepth=16 --rw=write --size=1000M --bssplit=8k/100 --direct=1 --filename=/dev/lsvbd1 --numjobs=1 

fio --name=fff --ioengine=io_uring --iodepth=16 --rw=read --size=1000M --bssplit=8k/100 --direct=1 --filename=/dev/lsvbd1 --numjobs=1 
