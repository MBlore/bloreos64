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

/*
 * Initialises a newly allocated cluster as a FAT32 directory.
 *
 * Creates the "." entry pointing to the directory itself and the
 * ".." entry pointing to its parent.
 */
bool fat32_directory_init_cluster(const fat32_volume_t *volume, uint32_t cluster,
                                  uint32_t parent_cluster)
{
    if (volume == NULL || volume->bytes_per_cluster < 3 * FAT32_DIRECTORY_ENTRY_SIZE) {
        return false;
    }

    if (cluster < FAT32_FIRST_DATA_CLUSTER ||
        cluster >= volume->cluster_count + FAT32_FIRST_DATA_CLUSTER) {
        return false;
    }

    if (parent_cluster < FAT32_FIRST_DATA_CLUSTER ||
        parent_cluster >= volume->cluster_count + FAT32_FIRST_DATA_CLUSTER) {
        return false;
    }

    uint8_t *buffer = kalloc(volume->bytes_per_cluster);

    if (buffer == NULL) {
        return false;
    }

    memset(buffer, 0, volume->bytes_per_cluster);

    fat32_directory_entry_t *entries = (fat32_directory_entry_t *)buffer;

    // "." points to this directory.
    memcpy(entries[0].name, ".          ", FAT32_SHORT_NAME_LENGTH);
    entries[0].attributes = FAT32_ATTR_DIRECTORY;
    entries[0].first_cluster_high = (uint16_t)(cluster >> 16);
    entries[0].first_cluster_low = (uint16_t)cluster;

    // ".." points to the parent directory.
    memcpy(entries[1].name, "..         ", FAT32_SHORT_NAME_LENGTH);
    entries[1].attributes = FAT32_ATTR_DIRECTORY;

    uint32_t stored_parent = parent_cluster == volume->root_cluster ? 0 : parent_cluster;

    entries[1].first_cluster_high = (uint16_t)(stored_parent >> 16);
    entries[1].first_cluster_low = (uint16_t)stored_parent;

    bool success = fat32_write_cluster(volume, cluster, buffer);

    kfree(buffer);

    return success;
}
