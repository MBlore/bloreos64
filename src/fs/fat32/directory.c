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

uint32_t fat32_get_first_cluster(const fat32_directory_entry_t *entry)
{
    uint32_t cluster = ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;

    // Retain only the lower 28 bits.
    return cluster & FAT32_CLUSTER_MASK;
}

void fat32_format_short_name(const fat32_directory_entry_t *entry, char *name)
{
    uint32_t position = 0;

    // Extract the base filename, excluding trailing padding.
    for (uint32_t i = 0; i < FAT32_SHORT_NAME_BASE_LENGTH; i++) {
        uint8_t character = entry->name[i];

        if (character == ' ') {
            break;
        }

        // Restore an escaped first character.
        if (i == 0 && character == FAT32_DIR_ENTRY_ESCAPED) {
            character = FAT32_DIR_ENTRY_DELETED;
        }

        // Apply the lowercase filename flag.
        if ((entry->nt_reserved & FAT32_NT_LOWERCASE_BASE) &&
            character >= 'A' && character <= 'Z') {
            character += 'a' - 'A';
        }

        name[position++] = character;
    }

    // Append the extension if one exists.
    if (entry->name[FAT32_SHORT_NAME_BASE_LENGTH] != ' ') {
        name[position++] = '.';

        for (uint32_t i = FAT32_SHORT_NAME_BASE_LENGTH; i < FAT32_SHORT_NAME_LENGTH; i++) {
            uint8_t character = entry->name[i];

            if (character == ' ') {
                break;
            }

            // Apply the lowercase extension flag.
            if ((entry->nt_reserved & FAT32_NT_LOWERCASE_EXT) &&
                character >= 'A' && character <= 'Z') {
                character += 'a' - 'A';
            }

            name[position++] = character;
        }
    }

    name[position] = '\0';
}


/*
 * Converts an ASCII filename into FAT32's 11-byte short-name format.
 *
 * Supports standard 8.3 filenames, converting lowercase letters to
 * uppercase and padding the name and extension with spaces.
 *
 * Returns false for invalid or unsupported filenames.
 */
bool fat32_make_short_name(const char *filename,
                           uint8_t name[FAT32_SHORT_NAME_LENGTH])
{
    if (filename == NULL || name == NULL || filename[0] == '\0') {
        return false;
    }

    memset(name, ' ', FAT32_SHORT_NAME_LENGTH);

    uint32_t base_length = 0;
    uint32_t extension_length = 0;
    bool in_extension = false;

    for (const char *cursor = filename; *cursor != '\0'; cursor++) {
        uint8_t character = (uint8_t)*cursor;

        if (character == '.') {
            // Only one separator is allowed, after a nonempty base name.
            if (in_extension || base_length == 0) {
                return false;
            }

            in_extension = true;
            continue;
        }

        // Accept a conservative subset of valid FAT32 short-name characters.
        bool valid = (character >= 'A' && character <= 'Z') ||
                     (character >= 'a' && character <= 'z') ||
                     (character >= '0' && character <= '9') ||
                     character == '_' || character == '-';

        if (!valid) {
            return false;
        }

        // FAT32 short names are conventionally stored in uppercase.
        if (character >= 'a' && character <= 'z') {
            character -= 'a' - 'A';
        }

        if (!in_extension) {
            if (base_length >= FAT32_SHORT_NAME_BASE_LENGTH) {
                return false;
            }

            name[base_length++] = character;
        } else {
            if (extension_length >= FAT32_SHORT_NAME_EXT_LENGTH) {
                return false;
            }

            name[FAT32_SHORT_NAME_BASE_LENGTH + extension_length++] = character;
        }
    }

    // Reject filenames ending with a dot.
    if (in_extension && extension_length == 0) {
        return false;
    }

    return true;
}

bool fat32_directory_open(const fat32_volume_t *volume, uint32_t cluster,
                          fat32_directory_iterator_t *iterator)
{
    if (volume == NULL || iterator == NULL) {
        return false;
    }

    memset(iterator, 0, sizeof(fat32_directory_iterator_t));

    // Allocate a buffer to hold one directory cluster.
    iterator->buffer = kalloc(volume->bytes_per_cluster);

    if (iterator->buffer == NULL) {
        return false;
    }

    // Read the first cluster of the directory.
    if (!fat32_read_cluster(volume, cluster, iterator->buffer)) {
        kfree(iterator->buffer);
        iterator->buffer = NULL;
        return false;
    }

    // Initialise the iterator state.
    iterator->volume = volume;
    iterator->current_cluster = cluster;
    iterator->entry_index = 0;
    iterator->clusters_visited = 1;
    iterator->finished = false;

    return true;
}

void fat32_directory_close(fat32_directory_iterator_t *iterator)
{
    if (iterator == NULL) {
        return;
    }

    // Release the allocated cluster buffer.
    if (iterator->buffer != NULL) {
        kfree(iterator->buffer);
    }

    // Reset the iterator state.
    memset(iterator, 0, sizeof(fat32_directory_iterator_t));
}

fat32_directory_status_t fat32_directory_next(fat32_directory_iterator_t *iterator,
                                             fat32_directory_record_t *record)
{
    if (iterator == NULL || record == NULL || iterator->buffer == NULL) {
        return FAT32_DIR_ERROR;
    }

    if (iterator->finished) {
        return FAT32_DIR_END;
    }

    const fat32_volume_t *volume = iterator->volume;
    uint32_t entry_count = volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    while (true) {
        if (iterator->entry_index >= entry_count) {
            uint32_t next_cluster;
            fat32_chain_status_t status =
                fat32_next_cluster(volume, iterator->current_cluster, &next_cluster);

            if (status == FAT32_CHAIN_END) {
                iterator->finished = true;
                return FAT32_DIR_END;
            }

            if (status == FAT32_CHAIN_ERROR) {
                return FAT32_DIR_ERROR;
            }

            if (iterator->clusters_visited >= volume->cluster_count) {
                return FAT32_DIR_ERROR;
            }

            if (!fat32_read_cluster(volume, next_cluster, iterator->buffer)) {
                return FAT32_DIR_ERROR;
            }

            iterator->current_cluster = next_cluster;
            iterator->entry_index = 0;
            iterator->clusters_visited++;
        }

        fat32_directory_entry_t *entries = (fat32_directory_entry_t *)iterator->buffer;
        uint32_t entry_index = iterator->entry_index++;
        fat32_directory_entry_t *current = &entries[entry_index];

        if (current->name[0] == FAT32_DIR_ENTRY_END) {
            iterator->finished = true;
            return FAT32_DIR_END;
        }

        if (current->name[0] == FAT32_DIR_ENTRY_DELETED) {
            iterator->lfn.active = false;
            continue;
        }

        if (current->attributes == FAT32_ATTR_LONG_NAME) {
            fat32_long_directory_entry_t long_entry;
            memcpy(&long_entry, current, sizeof(long_entry));

            fat32_lfn_process(&iterator->lfn, &long_entry);
            continue;
        }

        if (current->attributes & FAT32_ATTR_VOLUME_ID) {
            iterator->lfn.active = false;
            continue;
        }

        record->entry = *current;

        // Retain the physical location of the short directory entry.
        record->directory_cluster = iterator->current_cluster;
        record->directory_index = entry_index;

        // Prefer the long filename, falling back to the short filename.
        if (!fat32_format_long_name(&iterator->lfn, current, record->name)) {
            fat32_format_short_name(current, record->name);
        }

        // The long filename sequence has now been consumed.
        iterator->lfn.active = false;

        return FAT32_DIR_NEXT;
    }
}

fat32_lookup_status_t fat32_directory_find(const fat32_volume_t *volume, uint32_t cluster,
                                          const char *name, fat32_directory_record_t *record)
{
    if (volume == NULL || name == NULL || record == NULL || name[0] == '\0') {
        return FAT32_LOOKUP_ERROR;
    }

    fat32_directory_iterator_t iterator;

    if (!fat32_directory_open(volume, cluster, &iterator)) {
        return FAT32_LOOKUP_ERROR;
    }

    fat32_directory_status_t status;

    while ((status = fat32_directory_next(&iterator, record)) == FAT32_DIR_NEXT) {
        char short_name[FAT32_SHORT_NAME_BUFFER_SIZE];

        fat32_format_short_name(&record->entry, short_name);

        // Match against both the long filename and the short filename alias.
        if (strcasecmp(record->name, name) == 0 || strcasecmp(short_name, name) == 0) {
            fat32_directory_close(&iterator);
            return FAT32_LOOKUP_FOUND;
        }
    }

    fat32_directory_close(&iterator);

    if (status == FAT32_DIR_ERROR) {
        return FAT32_LOOKUP_ERROR;
    }

    return FAT32_LOOKUP_NOT_FOUND;
}

/*
 * Reads a directory entry from its physical location on disk.
 *
 * The index identifies a zero-based directory entry within the
 * supplied cluster.
 *
 * Returns true on success, or false on validation or I/O failure.
 */
bool fat32_directory_read_entry(const fat32_volume_t *volume, uint32_t cluster,
                                uint32_t index, fat32_directory_entry_t *entry)
{
    if (volume == NULL || entry == NULL) {
        return false;
    }

    // Ensure the directory entry index is within the cluster.
    uint32_t entry_count = volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (index >= entry_count) {
        return false;
    }

    uint8_t *buffer = kalloc(volume->bytes_per_cluster);

    if (buffer == NULL) {
        return false;
    }

    if (!fat32_read_cluster(volume, cluster, buffer)) {
        kfree(buffer);
        return false;
    }

    // Extract the requested directory entry.
    uint32_t offset = index * FAT32_DIRECTORY_ENTRY_SIZE;

    memcpy(entry, buffer + offset, FAT32_DIRECTORY_ENTRY_SIZE);

    kfree(buffer);

    return true;
}

/*
 * Writes a short directory entry to its physical location on disk.
 *
 * Preserves all other directory entries within the containing cluster.
 * The caller is responsible for providing a valid directory entry
 * and ensuring that the target cluster belongs to a directory.
 *
 * Returns true on success, or false on validation or I/O failure.
 */
bool fat32_directory_write_entry(const fat32_volume_t *volume, uint32_t cluster,
                                 uint32_t index, const fat32_directory_entry_t *entry)
{
    if (volume == NULL || entry == NULL) {
        return false;
    }

    // Ensure the directory entry index is within the cluster.
    uint32_t entry_count = volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (index >= entry_count) {
        return false;
    }

    uint8_t *buffer = kalloc(volume->bytes_per_cluster);

    if (buffer == NULL) {
        return false;
    }

    // Read the existing directory cluster.
    if (!fat32_read_cluster(volume, cluster, buffer)) {
        kfree(buffer);
        return false;
    }

    // Replace only the requested directory entry.
    uint32_t offset = index * FAT32_DIRECTORY_ENTRY_SIZE;

    memcpy(buffer + offset, entry, FAT32_DIRECTORY_ENTRY_SIZE);

    // Persist the modified directory cluster.
    bool success = fat32_write_cluster(volume, cluster, buffer);

    kfree(buffer);

    return success;
}



/*
 * Finds an available directory entry, extending the directory
 * cluster chain when all existing entries are occupied.
 *
 * Returns false on allocation, FAT traversal or disk I/O failure.
 */
bool fat32_directory_find_free_entry(const fat32_volume_t *volume,
                                     uint32_t directory_cluster,
                                     uint32_t *entry_cluster,
                                     uint32_t *entry_index)
{
    if (volume == NULL || entry_cluster == NULL || entry_index == NULL ||
        directory_cluster < FAT32_FIRST_DATA_CLUSTER) {
        return false;
    }

    uint8_t *buffer = kalloc(volume->bytes_per_cluster);

    if (buffer == NULL) {
        return false;
    }

    uint32_t current_cluster = directory_cluster;
    uint32_t entries_per_cluster =
        volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    bool found = false;

    for (uint32_t visited = 0; visited < volume->cluster_count; visited++) {
        if (!fat32_read_cluster(volume, current_cluster, buffer)) {
            break;
        }

        fat32_directory_entry_t *entries =
            (fat32_directory_entry_t *)buffer;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t marker = entries[i].name[0];

            if (marker == FAT32_DIR_ENTRY_DELETED ||
                marker == FAT32_DIR_ENTRY_END) {
                *entry_cluster = current_cluster;
                *entry_index = i;
                found = true;
                break;
            }
        }

        if (found) {
            break;
        }

        uint32_t next_cluster;
        fat32_chain_status_t status =
            fat32_next_cluster(volume, current_cluster, &next_cluster);

        if (status == FAT32_CHAIN_ERROR) {
            break;
        }

        if (status == FAT32_CHAIN_END) {
            // Only extend a directory when its existing chain is full.
            uint32_t new_cluster;

            if (fat32_extend_chain(volume, current_cluster, &new_cluster)) {
                *entry_cluster = new_cluster;
                *entry_index = 0;
                found = true;
            }

            break;
        }

        current_cluster = next_cluster;
    }

    kfree(buffer);

    return found;
}