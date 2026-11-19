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

/*
 * Creates a new directory at an absolute FAT32 path.
 *
 * Supports nested directories, short 8.3 names and long filenames.
 * The parent directory must already exist.
 *
 * Allocates and initialises the directory's first cluster before
 * publishing its entry in the parent directory.
 *
 * On failure, attempts to release the newly allocated cluster.
 * This operation is not atomic; disk write failures may leave
 * partially written directory entries.
 */
bool fat32_directory_create(const fat32_volume_t *volume, const char *path)
{
    if (volume == NULL || path == NULL) {
        return false;
    }

    uint32_t parent_cluster;
    char filename[FAT32_UTF8_NAME_BUFFER_SIZE];

    if (!fat32_path_resolve_parent(volume, path, &parent_cluster, filename)) {
        return false;
    }

    uint16_t long_name[FAT32_LFN_MAX_LENGTH];
    uint32_t name_length;

    if (!fat32_filename_to_utf16(filename, long_name, &name_length)) {
        return false;
    }

    // The name must not already exist in the parent directory.
    fat32_directory_record_t existing;

    if (fat32_directory_find(volume, parent_cluster, filename, &existing) !=
        FAT32_LOOKUP_NOT_FOUND) {
        return false;
    }

    uint8_t short_name[FAT32_SHORT_NAME_LENGTH];
    bool is_short_name = fat32_make_short_name(filename, short_name);

    if (is_short_name) {
        if (fat32_directory_short_name_exists(volume, parent_cluster, short_name) !=
            FAT32_SHORT_NAME_NOT_FOUND) {
            return false;
        }
    } else {
        if (!fat32_directory_find_short_alias(volume, parent_cluster, filename, short_name)) {
            return false;
        }
    }

    // Reserve space in the parent before allocating the new directory.
    uint32_t entry_cluster;
    uint32_t entry_index;

    if (is_short_name) {
        if (!fat32_directory_find_free_entry(volume, parent_cluster,
                                             &entry_cluster, &entry_index)) {
            return false;
        }
    } else {
        uint32_t lfn_entries =
            (name_length + FAT32_LFN_CHARS_PER_ENTRY - 1) / FAT32_LFN_CHARS_PER_ENTRY;

        if (!fat32_directory_find_free_entries(volume, parent_cluster, lfn_entries + 1,
                                                &entry_cluster, &entry_index)) {
            return false;
        }
    }

    // Allocate the first cluster of the new directory.
    uint32_t directory_cluster;

    if (!fat32_allocate_cluster(volume, &directory_cluster)) {
        return false;
    }

    if (!fat32_directory_init_cluster(volume, directory_cluster, parent_cluster)) {
        fat32_release_cluster(volume, directory_cluster);
        return false;
    }

    // Construct the directory entry stored in the parent.
    fat32_directory_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, short_name, FAT32_SHORT_NAME_LENGTH);

    entry.attributes = FAT32_ATTR_DIRECTORY;
    entry.first_cluster_high = (uint16_t)(directory_cluster >> 16);
    entry.first_cluster_low = (uint16_t)directory_cluster;

    bool success;

    if (!is_short_name) {
        success = fat32_directory_write_long_name(volume, entry_cluster, entry_index,
                                                  long_name, name_length, &entry);
    } else {
        fat32_directory_entry_t previous;

        success = fat32_directory_read_entry(volume, entry_cluster, entry_index, &previous);

        if (success) {
            success = previous.name[0] == FAT32_DIR_ENTRY_DELETED ||
                      previous.name[0] == FAT32_DIR_ENTRY_END;
        }

        if (success && previous.name[0] == FAT32_DIR_ENTRY_END) {
            uint32_t entries_per_cluster =
                volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

            if (entry_index + 1 < entries_per_cluster) {
                fat32_directory_entry_t end_entry;

                memset(&end_entry, 0, sizeof(end_entry));

                success = fat32_directory_write_entry(volume, entry_cluster,
                                                      entry_index + 1, &end_entry);
            }
        }

        if (success) {
            success = fat32_directory_write_entry(volume, entry_cluster, entry_index, &entry);
        }
    }

    if (!success) {
        // Best-effort cleanup. Partial parent entries may require recovery.
        fat32_release_cluster(volume, directory_cluster);
        return false;
    }

    return true;
}