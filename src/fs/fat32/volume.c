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

os_error_t fat32_read_boot_sector(partition_t *partition, fat32_boot_sector_t *boot_sector)
{
    if (partition == NULL || partition->device == NULL || boot_sector == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    if (partition->device->block_size != FAT32_BOOT_SECTOR_SIZE) {
        kprintf("FAT32: Unsupported logical sector size: %u\n", partition->device->block_size);
        return OS_ERR_NOT_SUPPORTED;
    }

    if (!partition_read(partition, 0, 1, boot_sector)) {
        kprintf("FAT32: Failed to read boot sector\n");
        return OS_ERR_IO;
    }

    return OS_OK;
}

/*
 * Validates FAT32 boot-sector fields and filesystem geometry.
 *
 * Returns a categorized error and prints the precise failed check
 * to the kernel console for diagnosis.
 */
os_error_t fat32_validate_boot_sector(partition_t *partition, const fat32_boot_sector_t *boot_sector)
{
    if (partition == NULL || partition->device == NULL || boot_sector == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    if (boot_sector->boot_signature != FAT32_BOOT_SIGNATURE) {
        kprintf("FAT32: Invalid boot signature: 0x%x\n", boot_sector->boot_signature);
        return OS_ERR_INVALID_FILESYSTEM;
    }

    if (boot_sector->bytes_per_sector != partition->device->block_size ||
        boot_sector->bytes_per_sector != FAT32_BOOT_SECTOR_SIZE) {
        kprintf("FAT32: Invalid bytes per sector: %u\n", boot_sector->bytes_per_sector);
        return OS_ERR_INVALID_GEOMETRY;
    }

    uint8_t spc = boot_sector->sectors_per_cluster;

    if (spc == 0 || spc > FAT32_MAX_SECTORS_PER_CLUSTER || (spc & (spc - 1)) != 0) {
        kprintf("FAT32: Invalid sectors per cluster: %u\n", spc);
        return OS_ERR_INVALID_GEOMETRY;
    }

    if (boot_sector->reserved_sector_count == 0 || boot_sector->fat_count == 0) {
        kprintf("FAT32: Invalid reserved sector count or FAT count\n");
        return OS_ERR_INVALID_GEOMETRY;
    }

    if (boot_sector->root_entry_count != 0 || boot_sector->fat_size_16 != 0 ||
        boot_sector->total_sectors_16 != 0) {
        kprintf("FAT32: Legacy FAT12/FAT16 geometry fields are nonzero\n");
        return OS_ERR_INVALID_FILESYSTEM;
    }

    if (boot_sector->fat_size_32 == 0 || boot_sector->total_sectors_32 == 0) {
        kprintf("FAT32: Missing FAT32 geometry fields\n");
        return OS_ERR_INVALID_GEOMETRY;
    }

    if (boot_sector->total_sectors_32 > partition->block_count) {
        kprintf("FAT32: Filesystem sectors: %u, partition sectors: %lu\n",
            boot_sector->total_sectors_32, (unsigned long)partition->block_count);
        return OS_ERR_INVALID_GEOMETRY;
    }

    if (boot_sector->filesystem_version != FAT32_FILESYSTEM_VERSION) {
        kprintf("FAT32: Unsupported filesystem version: %u\n", boot_sector->filesystem_version);
        return OS_ERR_UNSUPPORTED_FILESYSTEM;
    }

    if (boot_sector->root_cluster < FAT32_FIRST_DATA_CLUSTER) {
        kprintf("FAT32: Invalid root directory cluster: %u\n", boot_sector->root_cluster);
        return OS_ERR_INVALID_GEOMETRY;
    }

    uint64_t fat_sectors = (uint64_t)boot_sector->fat_count * boot_sector->fat_size_32;
    uint64_t first_data_sector = boot_sector->reserved_sector_count + fat_sectors;

    if (first_data_sector >= boot_sector->total_sectors_32) {
        kprintf("FAT32: Data region begins beyond filesystem capacity\n");
        return OS_ERR_INVALID_GEOMETRY;
    }

    uint64_t data_sectors = boot_sector->total_sectors_32 - first_data_sector;
    uint64_t cluster_count = data_sectors / boot_sector->sectors_per_cluster;
    uint64_t last_cluster = cluster_count + FAT32_FIRST_DATA_CLUSTER - 1;

    if (cluster_count < FAT32_MIN_CLUSTER_COUNT) {
        kprintf("FAT32: Data cluster count %lu is below minimum %u\n",
                (unsigned long)cluster_count, FAT32_MIN_CLUSTER_COUNT);
        kprintf("FAT32: Sectors per cluster: %u\n", spc);
        return OS_ERR_INVALID_GEOMETRY;
    }

    if (last_cluster > FAT32_MAX_CLUSTER_NUMBER) {
        kprintf("FAT32: Last cluster %lu exceeds supported range\n", (unsigned long)last_cluster);
        return OS_ERR_INVALID_GEOMETRY;
    }

    uint64_t fat_bytes = (uint64_t)boot_sector->fat_size_32 * boot_sector->bytes_per_sector;
    uint64_t fat_entries = fat_bytes / FAT32_ENTRY_SIZE;

    if (fat_entries <= last_cluster) {
        kprintf("FAT32: FAT contains %lu entries but needs %lu\n",
                (unsigned long)fat_entries, (unsigned long)(last_cluster + 1));
        return OS_ERR_INVALID_GEOMETRY;
    }

    if (boot_sector->root_cluster > last_cluster) {
        kprintf("FAT32: Root directory cluster exceeds data region\n");
        return OS_ERR_INVALID_GEOMETRY;
    }

    if (boot_sector->extended_flags & FAT32_MIRRORING_DISABLED) {
        uint32_t active_fat = boot_sector->extended_flags & FAT32_ACTIVE_FAT_MASK;

        if (active_fat >= boot_sector->fat_count) {
            kprintf("FAT32: Active FAT index %u is invalid\n", active_fat);
            return OS_ERR_INVALID_GEOMETRY;
        }
    }

    return OS_OK;
}

os_error_t fat32_init_volume(partition_t *partition, fat32_volume_t *volume)
{
    if (partition == NULL || volume == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    fat32_boot_sector_t boot_sector;

    os_error_t result = fat32_read_boot_sector(partition, &boot_sector);

    if (result != OS_OK) {
        return result;
    }

    result = fat32_validate_boot_sector(partition, &boot_sector);

    if (result != OS_OK) {
        return result;
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

    return OS_OK;
}