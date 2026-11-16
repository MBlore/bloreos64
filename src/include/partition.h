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
#ifndef _BLOREOS_PARTITION_H
#define _BLOREOS_PARTITION_H

#include <stdint.h>
#include <stdbool.h>
#include <block.h>

#define PARTITION_NAME_LENGTH 48
#define PARTITION_MAX_DEVICES 64

// Represents a logical partition within a block device.
typedef struct
{
    char name[PARTITION_NAME_LENGTH];

    block_device_t *device;

    uint32_t number;
    uint64_t first_lba;
    uint64_t block_count;
} partition_t;

bool partition_register(partition_t *partition);
partition_t *partition_find(const char *name);

bool partition_read(partition_t *partition, uint64_t lba, uint32_t count, void *buffer);
bool partition_write(partition_t *partition, uint64_t lba, uint32_t count, const void *buffer);

#endif