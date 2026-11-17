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
#include <stdbool.h>
#include <partition.h>
#include <mem.h>
#include <stddef.h>
#include <fat32.h>
#include <unicode.h>
#include <str.h>

bool fat32_read_boot_sector(partition_t *partition, fat32_boot_sector_t *boot_sector)
{
    if (partition == NULL || partition->device == NULL || boot_sector == NULL) {
        return false;
    }

    // Currently, only 512-byte logical sectors are supported.
    if (partition->device->block_size != FAT32_BOOT_SECTOR_SIZE) {
        return false;
    }

    return partition_read(partition, 0, 1, boot_sector);
}

bool fat32_validate_boot_sector(partition_t *partition, const fat32_boot_sector_t *boot_sector)
{
    if (partition == NULL || partition->device == NULL || boot_sector == NULL) {
        return false;
    }

    // Validate the boot sector signature.
    if (boot_sector->boot_signature != FAT32_BOOT_SIGNATURE) {
        return false;
    }

    // The filesystem sector size must match the device's logical block size.
    if (boot_sector->bytes_per_sector != partition->device->block_size ||
        boot_sector->bytes_per_sector != FAT32_BOOT_SECTOR_SIZE) {
        return false;
    }

    // Sectors per cluster must be a non-zero power of two.
    uint8_t spc = boot_sector->sectors_per_cluster;

    if (spc == 0 || spc > FAT32_MAX_SECTORS_PER_CLUSTER || (spc & (spc - 1)) != 0) {
        return false;
    }

    // A filesystem must contain reserved sectors and at least one FAT.
    if (boot_sector->reserved_sector_count == 0 || boot_sector->fat_count == 0) {
        return false;
    }

    // These legacy FAT12/FAT16 fields must be zero for FAT32.
    if (boot_sector->root_entry_count != 0 || boot_sector->fat_size_16 != 0 ||
        boot_sector->total_sectors_16 != 0) {
        return false;
    }

    // Validate the FAT32-specific geometry fields.
    if (boot_sector->fat_size_32 == 0 || boot_sector->total_sectors_32 == 0) {
        return false;
    }

    // Ensure the filesystem does not extend beyond the partition.
    if (boot_sector->total_sectors_32 > partition->block_count) {
        return false;
    }

    // Only FAT32 version 0.0 is currently supported.
    if (boot_sector->filesystem_version != FAT32_FILESYSTEM_VERSION) {
        return false;
    }

    // The first two cluster numbers are reserved.
    if (boot_sector->root_cluster < FAT32_FIRST_DATA_CLUSTER) {
        return false;
    }

    // Calculate the total number of sectors occupied by the FATs.
    uint64_t fat_sectors = (uint64_t)boot_sector->fat_count * boot_sector->fat_size_32;

    // Calculate where the data region begins.
    uint64_t first_data_sector = boot_sector->reserved_sector_count + fat_sectors;

    // Ensure there is space for the data region.
    if (first_data_sector >= boot_sector->total_sectors_32) {
        return false;
    }

    // Calculate the number of complete data clusters.
    uint64_t data_sectors = boot_sector->total_sectors_32 - first_data_sector;
    uint64_t cluster_count = data_sectors / boot_sector->sectors_per_cluster;

    // Validate the FAT32 cluster range.
    uint64_t last_cluster = cluster_count + FAT32_FIRST_DATA_CLUSTER - 1;

    if (cluster_count < FAT32_MIN_CLUSTER_COUNT || last_cluster > FAT32_MAX_CLUSTER_NUMBER) {
        return false;
    }

    // Ensure the FAT contains enough entries to describe all clusters.
    uint64_t fat_bytes = (uint64_t)boot_sector->fat_size_32 * boot_sector->bytes_per_sector;
    uint64_t fat_entries = fat_bytes / FAT32_ENTRY_SIZE;

    if (fat_entries <= last_cluster) {
        return false;
    }

    // Ensure the root directory cluster exists within the data region.
    if (boot_sector->root_cluster > last_cluster) {
        return false;
    }

    // Validate the active FAT index when mirroring is disabled.
    if (boot_sector->extended_flags & FAT32_MIRRORING_DISABLED) {
        uint32_t active_fat = boot_sector->extended_flags & FAT32_ACTIVE_FAT_MASK;

        if (active_fat >= boot_sector->fat_count) {
            return false;
        }
    }

    return true;
}

bool fat32_init_volume(partition_t *partition, fat32_volume_t *volume)
{
    if (partition == NULL || volume == NULL) {
        return false;
    }

    fat32_boot_sector_t boot_sector;

    // Read and validate the boot sector.
    if (!fat32_read_boot_sector(partition, &boot_sector)) {
        return false;
    }

    if (!fat32_validate_boot_sector(partition, &boot_sector)) {
        return false;
    }

    // Initialise the filesystem geometry.
    volume->partition = partition;
    volume->bytes_per_sector = boot_sector.bytes_per_sector;
    volume->sectors_per_cluster = boot_sector.sectors_per_cluster;
    volume->bytes_per_cluster = volume->bytes_per_sector * volume->sectors_per_cluster;
    volume->total_sectors = boot_sector.total_sectors_32;

    // Initialise the File Allocation Table geometry.
    volume->fat_count = boot_sector.fat_count;
    volume->sectors_per_fat = boot_sector.fat_size_32;
    volume->first_fat_sector = boot_sector.reserved_sector_count;

    // Default to the first FAT when mirroring is enabled.
    volume->active_fat_sector = volume->first_fat_sector;

    // Select the active FAT when mirroring is disabled.
    if (boot_sector.extended_flags & FAT32_MIRRORING_DISABLED) {
        uint32_t active_fat = boot_sector.extended_flags & FAT32_ACTIVE_FAT_MASK;

        volume->active_fat_sector = volume->first_fat_sector + active_fat * volume->sectors_per_fat;
    }

    volume->fat_mirroring = !(boot_sector.extended_flags & FAT32_MIRRORING_DISABLED);

    // Initialise the data region geometry.
    volume->first_data_sector = volume->first_fat_sector +
                                (volume->fat_count * volume->sectors_per_fat);

    uint32_t data_sectors = volume->total_sectors - volume->first_data_sector;

    volume->cluster_count = data_sectors / volume->sectors_per_cluster;
    volume->root_cluster = boot_sector.root_cluster;

    return true;
}