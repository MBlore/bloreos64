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

bool fat32_cluster_to_lba(const fat32_volume_t *volume, uint32_t cluster, uint64_t *lba)
{
    if (volume == NULL || lba == NULL) {
        return false;
    }

    // Reject reserved cluster numbers.
    if (cluster < FAT32_FIRST_DATA_CLUSTER) {
        return false;
    }

    // Convert the cluster number into a zero-based index.
    uint32_t cluster_index = cluster - FAT32_FIRST_DATA_CLUSTER;

    // Ensure the cluster exists within the data region.
    if (cluster_index >= volume->cluster_count) {
        return false;
    }

    // Calculate the partition-relative LBA.
    *lba = volume->first_data_sector + (uint64_t)cluster_index * volume->sectors_per_cluster;

    return true;
}

bool fat32_read_cluster(const fat32_volume_t *volume, uint32_t cluster, void *buffer)
{
    if (volume == NULL || buffer == NULL) {
        return false;
    }

    uint64_t lba;

    // Translate the cluster number into a partition-relative LBA.
    if (!fat32_cluster_to_lba(volume, cluster, &lba)) {
        return false;
    }

    // Read all sectors belonging to the cluster.
    return partition_read(volume->partition, lba, volume->sectors_per_cluster, buffer);
}

/*
 * Writes an entire data cluster to the volume.
 *
 * The supplied buffer must contain at least bytes_per_cluster bytes.
 * This operation does not modify the FAT or allocate a cluster.
 *
 * Returns true on success, or false on validation or I/O failure.
 */
bool fat32_write_cluster(const fat32_volume_t *volume, uint32_t cluster, const void *buffer)
{
    if (volume == NULL || buffer == NULL) {
        return false;
    }

    uint64_t lba;

    // Translate the cluster number into a partition-relative LBA.
    if (!fat32_cluster_to_lba(volume, cluster, &lba)) {
        return false;
    }

    // Write all sectors belonging to the cluster.
    return partition_write(volume->partition, lba, volume->sectors_per_cluster, buffer);
}

bool fat32_read_fat_entry(const fat32_volume_t *volume, uint32_t cluster, uint32_t *entry)
{
    if (volume == NULL || entry == NULL) {
        return false;
    }

    // Ensure the cluster exists within the data region.
    if (cluster < FAT32_FIRST_DATA_CLUSTER ||
        cluster - FAT32_FIRST_DATA_CLUSTER >= volume->cluster_count) {
        return false;
    }

    // Calculate the byte offset of the entry within the FAT.
    uint64_t fat_offset = (uint64_t)cluster * FAT32_ENTRY_SIZE;

    // Calculate the sector and byte offset within that sector.
    uint64_t fat_sector = volume->active_fat_sector + fat_offset / volume->bytes_per_sector;
    uint32_t entry_offset = fat_offset % volume->bytes_per_sector;

    // Read the sector containing the FAT entry.
    uint8_t buffer[FAT32_BOOT_SECTOR_SIZE];

    if (!partition_read(volume->partition, fat_sector, 1, buffer)) {
        return false;
    }

    // Extract the entry and discard the upper four reserved bits.
    uint32_t value;
    memcpy(&value, buffer + entry_offset, sizeof(value));

    *entry = value & FAT32_CLUSTER_MASK;

    return true;
}

/*
 * Updates a FAT entry.
 *
 * When FAT mirroring is enabled, all FAT copies are updated.
 * Otherwise, only the active FAT is modified.
 *
 * Returns true on success, or false on validation or I/O failure.
 */
bool fat32_write_fat_entry(const fat32_volume_t *volume, uint32_t cluster, uint32_t entry)
{
    if (volume == NULL) {
        return false;
    }

    // Ensure the target cluster exists within the data region.
    if (cluster < FAT32_FIRST_DATA_CLUSTER ||
        cluster - FAT32_FIRST_DATA_CLUSTER >= volume->cluster_count) {
        return false;
    }

    // Reject values exceeding the 28-bit FAT entry range.
    if (entry > FAT32_CLUSTER_MASK) {
        return false;
    }

    uint64_t fat_offset = (uint64_t)cluster * FAT32_ENTRY_SIZE;
    uint64_t sector_offset = fat_offset / volume->bytes_per_sector;
    uint32_t entry_offset = fat_offset % volume->bytes_per_sector;

    uint32_t first_sector = volume->fat_mirroring ?
                            volume->first_fat_sector : volume->active_fat_sector;

    uint32_t fat_count = volume->fat_mirroring ? volume->fat_count : 1;

    uint8_t buffer[FAT32_BOOT_SECTOR_SIZE];

    for (uint32_t i = 0; i < fat_count; i++) {
        uint64_t fat_sector = first_sector + (uint64_t)i * volume->sectors_per_fat;
        fat_sector += sector_offset;

        if (!partition_read(volume->partition, fat_sector, 1, buffer)) {
            return false;
        }

        // Preserve the upper four reserved bits of the existing entry.
        uint32_t value;
        memcpy(&value, buffer + entry_offset, sizeof(value));

        value = (value & ~FAT32_CLUSTER_MASK) | entry;

        memcpy(buffer + entry_offset, &value, sizeof(value));

        if (!partition_write(volume->partition, fat_sector, 1, buffer)) {
            return false;
        }
    }

    return true;
}

fat32_chain_status_t fat32_next_cluster(const fat32_volume_t *volume, uint32_t cluster,
                                       uint32_t *next_cluster)
{
    if (volume == NULL || next_cluster == NULL) {
        return FAT32_CHAIN_ERROR;
    }

    uint32_t entry;

    // Retrieve the FAT entry for the current cluster.
    if (!fat32_read_fat_entry(volume, cluster, &entry)) {
        return FAT32_CHAIN_ERROR;
    }

    // Check whether this is the final cluster in the chain.
    if (entry >= FAT32_CLUSTER_EOC_MIN) {
        return FAT32_CHAIN_END;
    }

    // Reject free, reserved and bad clusters.
    if (entry < FAT32_FIRST_DATA_CLUSTER || entry > FAT32_MAX_CLUSTER_NUMBER) {
        return FAT32_CHAIN_ERROR;
    }

    // Ensure the next cluster exists within the volume.
    if (entry - FAT32_FIRST_DATA_CLUSTER >= volume->cluster_count) {
        return FAT32_CHAIN_ERROR;
    }

    *next_cluster = entry;

    return FAT32_CHAIN_NEXT;
}