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
#ifndef _BLOREOS_GPT_H
#define _BLOREOS_GPT_H

#include <stdint.h>
#include <stdbool.h>
#include <block.h>

#define GPT_HEADER_LBA       1
#define GPT_SIGNATURE        "EFI PART"
#define GPT_SIGNATURE_LENGTH 8
#define GPT_GUID_LENGTH      16
#define GPT_HEADER_SIZE      92

// Supported GPT specification revision (1.0).
#define GPT_REVISION_1_0 0x00010000
#define GPT_MIN_PARTITION_ENTRY_SIZE 128

#define GPT_PARTITION_NAME_LENGTH 36
#define GPT_PARTITION_ENTRY_SIZE 128

// Maximum partition entry array allocation (1 MiB).
#define GPT_MAX_ENTRY_ARRAY_SIZE (1024U * 1024U)

// Represents an on-disk GPT header.
typedef struct __attribute__((packed))
{
    uint8_t signature[GPT_SIGNATURE_LENGTH];

    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;

    uint64_t current_lba;
    uint64_t alternate_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;

    uint8_t disk_guid[GPT_GUID_LENGTH];

    uint64_t partition_entry_lba;
    uint32_t partition_entry_count;
    uint32_t partition_entry_size;
    uint32_t partition_entry_crc32;
} gpt_header_t;

_Static_assert(
    sizeof(gpt_header_t) == GPT_HEADER_SIZE,
    "Invalid GPT header structure size"
);

// Represents an on-disk GPT partition entry.
typedef struct __attribute__((packed))
{
    uint8_t type_guid[GPT_GUID_LENGTH];
    uint8_t unique_guid[GPT_GUID_LENGTH];

    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attributes;

    uint16_t name[GPT_PARTITION_NAME_LENGTH];
} gpt_partition_entry_t;

_Static_assert(
    sizeof(gpt_partition_entry_t) == GPT_PARTITION_ENTRY_SIZE,
    "Invalid GPT partition entry structure size"
);

bool gpt_read_header(block_device_t *device, gpt_header_t *header);
void *gpt_read_entries(block_device_t *device, const gpt_header_t *header);
bool gpt_discover_partitions(block_device_t *device);

#endif