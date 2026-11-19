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
#include <stdint.h>
#include <stddef.h>
#include <fat32.h>
#include <mem.h>
#include <str.h>

/*
 * Determines whether a FAT32 directory contains any active entries
 * other than the special "." and ".." directory entries.
 *
 * Returns false for invalid clusters, read failures or corrupt chains.
 */
bool fat32_directory_is_empty(const fat32_volume_t *volume, uint32_t directory_cluster)
{
    if (volume == NULL || volume->bytes_per_cluster < FAT32_DIRECTORY_ENTRY_SIZE ||
        volume->bytes_per_cluster % FAT32_DIRECTORY_ENTRY_SIZE != 0 ||
        directory_cluster < FAT32_FIRST_DATA_CLUSTER ||
        directory_cluster >= volume->cluster_count + FAT32_FIRST_DATA_CLUSTER) {
        return false;
    }

    uint8_t *buffer = kalloc(volume->bytes_per_cluster);

    if (buffer == NULL) {
        return false;
    }

    uint32_t entries_per_cluster = volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;
    uint32_t current_cluster = directory_cluster;
    bool empty = false;

    for (uint32_t visited = 0; visited < volume->cluster_count; visited++) {
        if (!fat32_read_cluster(volume, current_cluster, buffer)) {
            break;
        }

        fat32_directory_entry_t *entries = (fat32_directory_entry_t *)buffer;
        bool contains_entries = false;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            fat32_directory_entry_t *entry = &entries[i];

            if (entry->name[0] == FAT32_DIR_ENTRY_END) {
                empty = true;
                goto done;
            }

            if (entry->name[0] == FAT32_DIR_ENTRY_DELETED) {
                continue;
            }

            if (entry->attributes == FAT32_ATTR_LONG_NAME) {
                contains_entries = true;
                break;
            }

            if (memcmp(entry->name, ".          ", FAT32_SHORT_NAME_LENGTH) == 0 ||
                memcmp(entry->name, "..         ", FAT32_SHORT_NAME_LENGTH) == 0) {
                continue;
            }

            contains_entries = true;
            break;
        }

        if (contains_entries) {
            break;
        }

        uint32_t next_cluster;

        fat32_chain_status_t status = fat32_next_cluster(volume, current_cluster, &next_cluster);

        if (status == FAT32_CHAIN_END) {
            empty = true;
            break;
        }

        if (status != FAT32_CHAIN_NEXT ||
            next_cluster < FAT32_FIRST_DATA_CLUSTER ||
            next_cluster >= volume->cluster_count + FAT32_FIRST_DATA_CLUSTER) {
            break;
        }

        current_cluster = next_cluster;
    }

done:
    kfree(buffer);
    return empty;
}


/*
 * Removes an empty FAT32 directory identified by its absolute path.
 *
 * Rejects the root directory, non-directory entries, read-only
 * directories and directories containing active entries.
 *
 * Validates any associated LFN entries before removing them.
 * Unlinks the directory from its parent before releasing its
 * allocated cluster chain.
 *
 * This operation is not atomic. A failure while releasing the
 * cluster chain may leave orphaned clusters.
 */
bool fat32_directory_delete(const fat32_volume_t *volume, const char *path)
{
    if (volume == NULL || path == NULL) {
        return false;
    }

    uint32_t parent_cluster;
    char filename[FAT32_UTF8_NAME_BUFFER_SIZE];

    if (!fat32_path_resolve_parent(volume, path, &parent_cluster, filename)) {
        return false;
    }

    fat32_directory_record_t record;

    if (fat32_directory_find(volume, parent_cluster, filename, &record) !=
        FAT32_LOOKUP_FOUND) {
        return false;
    }

    // Only ordinary directories may be removed.
    if (!(record.entry.attributes & FAT32_ATTR_DIRECTORY) ||
        (record.entry.attributes & (FAT32_ATTR_READ_ONLY | FAT32_ATTR_VOLUME_ID))) {
        return false;
    }

    // Validate the short name or the complete long filename sequence.
    if (record.lfn_entry_count == 0) {
        uint8_t short_name[FAT32_SHORT_NAME_LENGTH];

        if (!fat32_make_short_name(filename, short_name) ||
            memcmp(record.entry.name, short_name, FAT32_SHORT_NAME_LENGTH) != 0) {
            return false;
        }
    } else {
        if (!fat32_directory_validate_lfn(volume, parent_cluster, &record)) {
            return false;
        }
    }

    uint32_t directory_cluster = fat32_get_first_cluster(&record.entry);

    // Never remove the root directory or an invalid cluster.
    if (directory_cluster == volume->root_cluster ||
        directory_cluster < FAT32_FIRST_DATA_CLUSTER ||
        directory_cluster >= volume->cluster_count + FAT32_FIRST_DATA_CLUSTER) {
        return false;
    }

    // A directory must contain no active entries other than "." and "..".
    if (!fat32_directory_is_empty(volume, directory_cluster)) {
        return false;
    }

    // Remove associated LFN entries before deleting the short entry.
    if (record.lfn_entry_count != 0) {
        if (!fat32_directory_delete_lfn(volume, parent_cluster, &record)) {
            return false;
        }
    }

    fat32_directory_entry_t deleted_entry = record.entry;
    deleted_entry.name[0] = FAT32_DIR_ENTRY_DELETED;

    if (!fat32_directory_write_entry(volume, record.directory_cluster,
                                     record.directory_index, &deleted_entry)) {
        // Best-effort restoration of LFN entries.
        if (record.lfn_entry_count != 0) {
            fat32_directory_restore_lfn(volume, parent_cluster, &record);
        }

        return false;
    }

    // The directory is no longer visible. Release its cluster chain.
    if (!fat32_release_chain(volume, directory_cluster)) {
        return false;
    }

    return true;
}
