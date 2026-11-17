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
#include <stddef.h>
#include <stdint.h>
#include <partition.h>
#include <fat32.h>
#include <mem.h>

/*
 * Searches the FAT for an unallocated data cluster.
 *
 * The discovered cluster number is returned through the output parameter.
 * This operation does not allocate the cluster or modify the filesystem.
 *
 * Returns true if a free cluster is found, or false if no free cluster
 * exists or an I/O error occurs.
 */
bool fat32_find_free_cluster(const fat32_volume_t *volume, uint32_t *cluster)
{
    if (volume == NULL || cluster == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < volume->cluster_count; i++) {
        uint32_t candidate = FAT32_FIRST_DATA_CLUSTER + i;
        uint32_t entry;

        if (!fat32_read_fat_entry(volume, candidate, &entry)) {
            return false;
        }

        if (entry == FAT32_CLUSTER_FREE) {
            *cluster = candidate;
            return true;
        }
    }

    return false;
}

/*
 * Allocates and zero-initialises a free data cluster.
 *
 * The allocated cluster is marked as the end of an independent
 * cluster chain and is not yet associated with a file or directory.
 *
 * Returns true on success and provides the allocated cluster number,
 * or false on allocation or I/O failure.
 */
bool fat32_allocate_cluster(const fat32_volume_t *volume, uint32_t *cluster)
{
    if (volume == NULL || cluster == NULL) {
        return false;
    }

    uint32_t candidate;

    if (!fat32_find_free_cluster(volume, &candidate)) {
        return false;
    }

    uint8_t *buffer = kalloc(volume->bytes_per_cluster);

    if (buffer == NULL) {
        return false;
    }

    // Clear any data left behind by previous allocations.
    memset(buffer, 0, volume->bytes_per_cluster);

    bool success = fat32_write_cluster(volume, candidate, buffer);

    kfree(buffer);

    if (!success) {
        return false;
    }

    // Mark the cluster as an independent end-of-chain.
    if (!fat32_write_fat_entry(volume, candidate, FAT32_CLUSTER_EOC)) {
        return false;
    }

    *cluster = candidate;

    return true;
}

/*
 * Releases an individual allocated cluster.
 *
 * Only an end-of-chain cluster can be released by this operation.
 * The caller must ensure that no file, directory or other cluster
 * references it before releasing it.
 *
 * Returns true on success, or false on validation or I/O failure.
 */
bool fat32_release_cluster(const fat32_volume_t *volume, uint32_t cluster)
{
    if (volume == NULL || cluster == volume->root_cluster) {
        return false;
    }

    uint32_t entry;

    if (!fat32_read_fat_entry(volume, cluster, &entry)) {
        return false;
    }

    // Only release clusters marked as the end of a chain.
    if (entry < FAT32_CLUSTER_EOC_MIN) {
        return false;
    }

    // Return the cluster to the pool of available storage.
    return fat32_write_fat_entry(volume, cluster, FAT32_CLUSTER_FREE);
}

/*
 * Extends an existing cluster chain by allocating a new tail cluster.
 *
 * The supplied tail must currently be marked as end-of-chain.
 * The newly allocated cluster becomes the new end of the chain.
 *
 * Returns true on success and provides the new cluster number,
 * or false on validation, allocation or I/O failure.
 */
bool fat32_extend_chain(const fat32_volume_t *volume, uint32_t tail, uint32_t *cluster)
{
    if (volume == NULL || cluster == NULL) {
        return false;
    }

    uint32_t entry;

    if (!fat32_read_fat_entry(volume, tail, &entry)) {
        return false;
    }

    // Ensure the supplied cluster is currently the end of a chain.
    if (entry < FAT32_CLUSTER_EOC_MIN) {
        return false;
    }

    uint32_t candidate;

    if (!fat32_allocate_cluster(volume, &candidate)) {
        return false;
    }

    // Link the existing tail to the newly allocated cluster.
    if (!fat32_write_fat_entry(volume, tail, candidate)) {
        return false;
    }

    *cluster = candidate;

    return true;
}

/*
 * Releases every cluster belonging to an existing cluster chain.
 *
 * The caller must ensure that the chain is no longer referenced
 * by any file, directory or other cluster chain.
 *
 * Returns true on success, or false on validation or I/O failure.
 * A failure may leave the chain partially released.
 */
bool fat32_release_chain(const fat32_volume_t *volume, uint32_t first_cluster)
{
    if (volume == NULL) {
        return false;
    }

    // An empty file may have no allocated clusters.
    if (first_cluster == FAT32_CLUSTER_FREE) {
        return true;
    }

    uint32_t current = first_cluster;

    for (uint32_t i = 0; i < volume->cluster_count; i++) {
        if (current == volume->root_cluster) {
            return false;
        }

        // Retrieve the next cluster before releasing the current one.
        uint32_t next;
        fat32_chain_status_t status = fat32_next_cluster(volume, current, &next);

        if (status == FAT32_CHAIN_ERROR) {
            return false;
        }

        // Return the current cluster to the pool of free clusters.
        if (!fat32_write_fat_entry(volume, current, FAT32_CLUSTER_FREE)) {
            return false;
        }

        if (status == FAT32_CHAIN_END) {
            return true;
        }

        current = next;
    }

    return false;
}