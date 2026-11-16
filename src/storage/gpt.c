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
#include <gpt.h>
#include <mem.h>
#include <str.h>
#include <crc32.h>
#include <partition.h>

// Validate the basic structure of a primary GPT header.
static bool gpt_validate_header(const block_device_t *device, const gpt_header_t *header)
{
    // Verify the GPT signature.
    if (memcmp(header->signature,
               GPT_SIGNATURE,
               GPT_SIGNATURE_LENGTH) != 0) {
        return false;
    }

    // Verify the supported GPT revision.
    if (header->revision != GPT_REVISION_1_0) {
        return false;
    }

    // Ensure the header fits within a logical block.
    if (header->header_size < GPT_HEADER_SIZE ||
        header->header_size > device->block_size) {
        return false;
    }

    // The reserved field must be zero.
    if (header->reserved != 0) {
        return false;
    }

    // Validate the primary and backup header locations.
    if (header->current_lba != GPT_HEADER_LBA ||
        header->alternate_lba != device->block_count - 1) {
        return false;
    }

    // Validate the usable storage range.
    if (header->first_usable_lba <= header->current_lba ||
        header->first_usable_lba > header->last_usable_lba ||
        header->last_usable_lba >= header->alternate_lba) {
        return false;
    }

    // Validate the partition entry metadata.
    if (header->partition_entry_count == 0 ||
        header->partition_entry_size < GPT_MIN_PARTITION_ENTRY_SIZE ||
        header->partition_entry_lba <= header->current_lba ||
        header->partition_entry_lba >= header->first_usable_lba) {
        return false;
    }

    // GPT entry sizes must be a power-of-two multiple of 128.
    const uint32_t entry_size = header->partition_entry_size;

    if (entry_size % GPT_PARTITION_ENTRY_SIZE != 0 ||
        (entry_size & (entry_size - 1)) != 0) {
        return false;
    }

    // Calculate the total size of the partition entry array.
    const uint64_t array_size =
        (uint64_t)header->partition_entry_count *
        header->partition_entry_size;

    // Calculate the number of logical blocks occupied by the array.
    const uint64_t array_blocks =
        array_size / device->block_size +
        (array_size % device->block_size != 0);

    // Ensure the partition entry array does not overlap usable storage.
    if (array_blocks >
        header->first_usable_lba - header->partition_entry_lba) {
        return false;
    }

    return true;
}

// Verify the checksum of the complete on-disk GPT header.
static bool gpt_validate_header_crc32(void *buffer)
{
    gpt_header_t *header = buffer;

    // Preserve the checksum stored on disk.
    const uint32_t expected_crc32 = header->header_crc32;

    // The checksum field must be zero during calculation.
    header->header_crc32 = 0;

    const uint32_t calculated_crc32 =
        crc32(buffer, header->header_size);

    // Restore the original checksum.
    header->header_crc32 = expected_crc32;

    return calculated_crc32 == expected_crc32;
}

// Read the primary GPT header from a block device.
bool gpt_read_header(block_device_t *device, gpt_header_t *header)
{
    if (device == NULL ||
        header == NULL ||
        device->block_size < GPT_HEADER_SIZE) {
        return false;
    }

    // Allocate enough memory for an entire logical block.
    void *buffer = kalloc(device->block_size);

    if (buffer == NULL) {
        return false;
    }

    if (!block_read(device, GPT_HEADER_LBA, 1, buffer)) {
        kfree(buffer);
        return false;
    }

    // Extract the GPT header from the logical block.
    memcpy(header, buffer, sizeof(gpt_header_t));

    // Validate the header structure before calculating its checksum.
    if (!gpt_validate_header(device, header)) {
        kfree(buffer);
        return false;
    }

    // Validate the checksum using the original on-disk bytes.
    const bool valid = gpt_validate_header_crc32(buffer);

    kfree(buffer);

    return valid;
}

// Read the complete GPT partition entry array.
void *gpt_read_entries(block_device_t *device, const gpt_header_t *header)
{
    if (device == NULL ||
        header == NULL ||
        device->block_size < GPT_HEADER_SIZE) {
        return NULL;
    }

    if (!gpt_validate_header(device, header)) {
        return NULL;
    }

    const uint64_t array_size =
        (uint64_t)header->partition_entry_count *
        header->partition_entry_size;

    // Calculate the number of logical blocks required.
    const uint64_t block_count =
        array_size / device->block_size +
        (array_size % device->block_size != 0);

    const uint64_t buffer_size =
        block_count * device->block_size;

    // Prevent excessive kernel memory allocations.
    if (buffer_size > GPT_MAX_ENTRY_ARRAY_SIZE) {
        return NULL;
    }

    void *buffer = kalloc((size_t)buffer_size);

    if (buffer == NULL) {
        return NULL;
    }

    // Read the partition entry array from the disk.
    if (!block_read(device,
                    header->partition_entry_lba,
                    (uint32_t)block_count,
                    buffer)) {
        kfree(buffer);
        return NULL;
    }

    // Validate the partition entry array checksum.
    const uint32_t calculated_crc32 =
        crc32(buffer, (size_t)array_size);

    if (calculated_crc32 != header->partition_entry_crc32) {
        kfree(buffer);
        return NULL;
    }

    return buffer;
}

// Discover and register partitions on a GPT-formatted device.
bool gpt_discover_partitions(block_device_t *device)
{
    gpt_header_t header = {0};

    if (!gpt_read_header(device, &header)) {
        return false;
    }

    void *entries = gpt_read_entries(device, &header);

    if (entries == NULL) {
        return false;
    }

    const uint8_t empty_guid[GPT_GUID_LENGTH] = {0};

    for (uint32_t i = 0; i < header.partition_entry_count; i++) {

        const gpt_partition_entry_t *entry =
            (const gpt_partition_entry_t *)(
                (const uint8_t *)entries +
                ((size_t)i * header.partition_entry_size)
            );

        // Ignore unused partition entries.
        if (memcmp(entry->type_guid,
                   empty_guid,
                   GPT_GUID_LENGTH) == 0) {
            continue;
        }

        // Validate the partition boundaries.
        if (entry->first_lba < header.first_usable_lba ||
            entry->last_lba > header.last_usable_lba ||
            entry->first_lba > entry->last_lba) {
            continue;
        }

        // Allocate a persistent partition descriptor.
        partition_t *partition = kalloc(sizeof(partition_t));

        if (partition == NULL) {
            kfree(entries);
            return false;
        }

        memset(partition, 0, sizeof(partition_t));

        partition->device = device;
        partition->number = i + 1;
        partition->first_lba = entry->first_lba;
        partition->block_count =
            entry->last_lba - entry->first_lba + 1;

        snprintf(partition->name,
                 sizeof(partition->name),
                 "%sp%d",
                 device->name,
                 (uint64_t)partition->number);

        if (!partition_register(partition)) {
            kfree(partition);
            continue;
        }

        kprintf("GPT: Registered partition '%s'.\n",
            partition->name);
    }

    kfree(entries);

    return true;
}