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
#include <stddef.h>
#include <partition.h>
#include <str.h>

static partition_t *partitions[PARTITION_MAX_DEVICES];
static uint32_t partition_count = 0;

// Register a partition within the kernel.
bool partition_register(partition_t *partition)
{
    if (partition == NULL ||
        partition->device == NULL ||
        partition->name[0] == '\0' ||
        partition->number == 0 ||
        partition->block_count == 0) {
        return false;
    }

    // Ensure the partition name is null-terminated.
    if (partition->name[PARTITION_NAME_LENGTH - 1] != '\0') {
        return false;
    }

    // Ensure there is capacity in the partition registry.
    if (partition_count >= PARTITION_MAX_DEVICES) {
        return false;
    }

    // Ensure the partition fits within its parent block device.
    if (partition->first_lba >= partition->device->block_count ||
        partition->block_count >
            partition->device->block_count - partition->first_lba) {
        return false;
    }

    // Prevent duplicate partition names.
    if (partition_find(partition->name) != NULL) {
        return false;
    }

    // Calculate the inclusive boundaries of the new partition.
    const uint64_t first_lba = partition->first_lba;
    const uint64_t last_lba =
        first_lba + partition->block_count - 1;

    // Prevent overlapping partitions on the same block device.
    for (uint32_t i = 0; i < partition_count; i++) {
        const partition_t *existing = partitions[i];

        if (existing->device != partition->device) {
            continue;
        }

        const uint64_t existing_first = existing->first_lba;
        const uint64_t existing_last =
            existing_first + existing->block_count - 1;

        if (first_lba <= existing_last &&
            last_lba >= existing_first) {
            return false;
        }
    }

    partitions[partition_count++] = partition;

    return true;
}

// Find a registered partition by its name.
partition_t *partition_find(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    for (uint32_t i = 0; i < partition_count; i++) {
        if (strcmp(partitions[i]->name, name) == 0) {
            return partitions[i];
        }
    }

    return NULL;
}

// Read logical blocks relative to the start of a partition.
bool partition_read(partition_t *partition,
                    uint64_t lba,
                    uint32_t count,
                    void *buffer)
{
    if (partition == NULL ||
        partition->device == NULL ||
        buffer == NULL ||
        count == 0) {
        return false;
    }

    // Ensure the requested blocks remain within the partition.
    if (lba >= partition->block_count ||
        count > partition->block_count - lba) {
        return false;
    }

    // Translate the partition-relative LBA into a device LBA.
    const uint64_t device_lba = partition->first_lba + lba;

    return block_read(partition->device,
                      device_lba,
                      count,
                      buffer);
}

// Write logical blocks relative to the start of a partition.
bool partition_write(partition_t *partition,
                     uint64_t lba,
                     uint32_t count,
                     const void *buffer)
{
    if (partition == NULL ||
        partition->device == NULL ||
        buffer == NULL ||
        count == 0) {
        return false;
    }

    // Ensure the requested blocks remain within the partition.
    if (lba >= partition->block_count ||
        count > partition->block_count - lba) {
        return false;
    }

    // Translate the partition-relative LBA into a device LBA.
    const uint64_t device_lba = partition->first_lba + lba;

    return block_write(partition->device,
                       device_lba,
                       count,
                       buffer);
}