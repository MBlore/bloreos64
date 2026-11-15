#!/bin/bash

set -e

# Create an empty 128 MiB disk image.
rm -f data.hdd
dd if=/dev/zero bs=1M count=0 seek=128 of=data.hdd

# Create a GPT partition table.
/usr/sbin/sgdisk data.hdd -n 1:2048 -t 1:0700

# Format the partition as FAT32.
mformat -F -i data.hdd@@1M ::

# Copy the test files into the filesystem.
mcopy -s -i data.hdd@@1M disk-files/* ::/