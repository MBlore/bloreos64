/*
    BloreOS - Operating System
    Copyright (C) 2023 Martin Blore

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef _BLOREOS_BLOCK_H
#define _BLOREOS_BLOCK_H

#include <stdint.h>
#include <stdbool.h>

#define BLOCK_DEVICE_NAME_LENGTH    32
#define BLOCK_MAX_DEVICES 32

// An abstract block device that supports reading and writing data.
typedef struct block_device
{
    char name[BLOCK_DEVICE_NAME_LENGTH];        // Device indentifier, e.g. "nvme0n1".

    // Storage geometry.
    uint32_t block_size;
    uint64_t block_count;

    // Hardware specific context data.
    void *context;

    // Read one or more consecutive logical blocks.
    bool (*read)(void *context, uint64_t lba, uint32_t count, void *buffer);

    // Write one or more consecutive logical blocks.
    bool (*write)(void *context, uint64_t lba, uint32_t count, const void *buffer);
} block_device_t;

// Register a block device with the kernel.
bool block_register(block_device_t *device);

// Find a block device by its name.
block_device_t *block_find(const char *name);

// Read from a device.
bool block_read(block_device_t *device, uint64_t lba, uint32_t count, void *buffer);

// Write to a device.
bool block_write(block_device_t *device, uint64_t lba, uint32_t count, const void *buffer);

uint32_t block_get_count(void);
block_device_t *block_get_device(uint32_t index);

#endif