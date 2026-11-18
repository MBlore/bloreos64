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


/*
 * Generates a numbered FAT32 8.3 alias for a long filename.
 *
 * The sequence number determines the ~N suffix. This function
 * only formats an alias; it does not check directory collisions.
 *
 * Currently supports ASCII filenames. Unsupported characters
 * are removed from the alias, not from the original long name.
 */
bool fat32_make_short_alias(const char *filename, uint32_t sequence,
                            uint8_t name[FAT32_SHORT_NAME_LENGTH])
{
    if (filename == NULL || name == NULL || filename[0] == '\0' ||
        sequence == 0 || sequence > 999999) {
        return false;
    }

    const char *extension = NULL;
    const char *end = filename;

    for (const char *cursor = filename; *cursor != '\0'; cursor++) {
        uint8_t character = (uint8_t)*cursor;

        // UTF-8 alias generation will be handled separately.
        if (character >= 0x80 || character == '/' || character == '\\') {
            return false;
        }

        if (character == '.') {
            extension = cursor;
        }

        end = cursor + 1;
    }

    if (extension == filename) {
        return false;
    }

    // Ignore a trailing dot when selecting the extension.
    if (extension == end - 1) {
        extension = NULL;
    }

    char suffix[8];
    uint32_t suffix_length = 0;

    suffix[suffix_length++] = '~';

    char digits[6];
    uint32_t digit_count = 0;

    do {
        digits[digit_count++] = '0' + (sequence % 10);
        sequence /= 10;
    } while (sequence != 0);

    while (digit_count > 0) {
        suffix[suffix_length++] = digits[--digit_count];
    }

    uint32_t base_capacity =
        FAT32_SHORT_NAME_BASE_LENGTH - suffix_length;

    memset(name, ' ', FAT32_SHORT_NAME_LENGTH);

    const char *base_end = extension != NULL ? extension : end;
    uint32_t base_length = 0;

    for (const char *cursor = filename; cursor < base_end; cursor++) {
        uint8_t character = (uint8_t)*cursor;

        bool valid = (character >= 'A' && character <= 'Z') ||
                     (character >= 'a' && character <= 'z') ||
                     (character >= '0' && character <= '9') ||
                     character == '_';

        if (!valid) {
            continue;
        }

        if (character >= 'a' && character <= 'z') {
            character -= 'a' - 'A';
        }

        if (base_length < base_capacity) {
            name[base_length++] = character;
        }
    }

    if (base_length == 0) {
        return false;
    }

    memcpy(name + base_length, suffix, suffix_length);

    if (extension != NULL) {
        uint32_t extension_length = 0;

        for (const char *cursor = extension + 1;
             *cursor != '\0' && extension_length < FAT32_SHORT_NAME_EXT_LENGTH;
             cursor++) {
            uint8_t character = (uint8_t)*cursor;

            bool valid = (character >= 'A' && character <= 'Z') ||
                         (character >= 'a' && character <= 'z') ||
                         (character >= '0' && character <= '9') ||
                         character == '_';

            if (!valid) {
                continue;
            }

            if (character >= 'a' && character <= 'z') {
                character -= 'a' - 'A';
            }

            name[FAT32_SHORT_NAME_BASE_LENGTH + extension_length++] = character;
        }
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

        // Only associate LFN entries with the file when the complete sequence is valid.
        record->lfn_entry_count = 0;

        if (fat32_format_long_name(&iterator->lfn, current, record->name)) {
            record->lfn_entry_count = iterator->lfn.entry_count;
        } else {
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


/*
 * Finds a consecutive run of free directory entries.
 *
 * Deleted entries are reusable. Once an end-of-directory marker
 * is encountered, all subsequent slots are logically unused.
 *
 * The directory chain is extended if the available run reaches
 * its end before enough entries have been found.
 *
 * This operation may allocate clusters but does not write entries.
 */
bool fat32_directory_find_free_entries(const fat32_volume_t *volume,
                                       uint32_t directory_cluster,
                                       uint32_t required_entries,
                                       uint32_t *entry_cluster,
                                       uint32_t *entry_index)
{
    if (volume == NULL || entry_cluster == NULL || entry_index == NULL ||
        directory_cluster < FAT32_FIRST_DATA_CLUSTER ||
        required_entries == 0 ||
        required_entries > FAT32_LFN_MAX_ENTRIES + 1) {
        return false;
    }

    uint32_t entries_per_cluster =
        volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (entries_per_cluster == 0) {
        return false;
    }

    uint8_t *buffer = kalloc(volume->bytes_per_cluster);

    if (buffer == NULL) {
        return false;
    }

    uint32_t current_cluster = directory_cluster;
    uint32_t run_cluster = 0;
    uint32_t run_index = 0;
    uint32_t run_length = 0;
    uint32_t clusters_visited = 0;

    bool after_end_marker = false;
    bool found = false;

    while (clusters_visited < volume->cluster_count) {
        if (!fat32_read_cluster(volume, current_cluster, buffer)) {
            break;
        }

        clusters_visited++;

        fat32_directory_entry_t *entries =
            (fat32_directory_entry_t *)buffer;

        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            uint8_t marker = entries[i].name[0];

            if (marker == FAT32_DIR_ENTRY_END) {
                after_end_marker = true;
            }

            bool available = after_end_marker ||
                             marker == FAT32_DIR_ENTRY_DELETED;

            if (available) {
                if (run_length == 0) {
                    run_cluster = current_cluster;
                    run_index = i;
                }

                run_length++;

                if (run_length == required_entries) {
                    *entry_cluster = run_cluster;
                    *entry_index = run_index;
                    found = true;
                    break;
                }
            } else {
                // An occupied entry interrupts the current run.
                run_length = 0;
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
            /*
             * The current chain does not contain enough space.
             * Allocate another cluster and continue the run.
             *
             * The newly allocated cluster is zero-initialised
             * by fat32_allocate_cluster().
             */
            if (clusters_visited >= volume->cluster_count) {
                break;
            }

            if (!fat32_extend_chain(volume, current_cluster, &next_cluster)) {
                break;
            }
        }

        current_cluster = next_cluster;
    }

    kfree(buffer);

    return found;
}


/*
 * Advances a directory entry position within an existing FAT chain.
 *
 * Returns false if the position is at the end of the chain or
 * the FAT cannot be read.
 */
static bool fat32_directory_advance_entry(const fat32_volume_t *volume,
                                          uint32_t *cluster,
                                          uint32_t *index)
{
    uint32_t entries_per_cluster =
        volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (*index + 1 < entries_per_cluster) {
        (*index)++;
        return true;
    }

    uint32_t next_cluster;

    if (fat32_next_cluster(volume, *cluster, &next_cluster) != FAT32_CHAIN_NEXT) {
        return false;
    }

    *cluster = next_cluster;
    *index = 0;

    return true;
}


/*
 * Writes an LFN sequence followed by its short directory entry.
 *
 * All target entries are validated and backed up before writing.
 * If a write fails, the failed entry and previously modified entries
 * are restored using their original contents.
 *
 * Rollback is best-effort. This does not guarantee crash consistency
 * or recovery from persistent disk I/O failures.
 */
bool fat32_directory_write_long_name(const fat32_volume_t *volume,
                                     uint32_t entry_cluster,
                                     uint32_t entry_index,
                                     const uint16_t *name,
                                     uint32_t name_length,
                                     const fat32_directory_entry_t *short_entry)
{
    if (volume == NULL || name == NULL || short_entry == NULL ||
        name_length == 0 || name_length > FAT32_LFN_MAX_LENGTH ||
        entry_cluster < FAT32_FIRST_DATA_CLUSTER) {
        return false;
    }

    uint32_t entries_per_cluster =
        volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (entries_per_cluster == 0 || entry_index >= entries_per_cluster) {
        return false;
    }

    uint32_t lfn_count =
        (name_length + FAT32_LFN_CHARS_PER_ENTRY - 1) /
        FAT32_LFN_CHARS_PER_ENTRY;

    uint32_t total_entries = lfn_count + 1;

    // Save the physical position and original contents of every target entry.
    uint32_t clusters[FAT32_LFN_MAX_ENTRIES + 1];
    uint32_t indices[FAT32_LFN_MAX_ENTRIES + 1];
    fat32_directory_entry_t originals[FAT32_LFN_MAX_ENTRIES + 1];

    uint32_t cluster = entry_cluster;
    uint32_t index = entry_index;
    bool after_end_marker = false;

    // Validate all target entries before modifying the directory.
    for (uint32_t i = 0; i < total_entries; i++) {
        clusters[i] = cluster;
        indices[i] = index;

        if (!fat32_directory_read_entry(volume, cluster, index, &originals[i])) {
            return false;
        }

        uint8_t marker = originals[i].name[0];

        if (marker == FAT32_DIR_ENTRY_END) {
            after_end_marker = true;
        }

        if (!after_end_marker && marker != FAT32_DIR_ENTRY_DELETED) {
            return false;
        }

        if (i + 1 < total_entries &&
            !fat32_directory_advance_entry(volume, &cluster, &index)) {
            return false;
        }
    }

    // Locate the entry immediately following the short-name entry.
    uint32_t end_cluster = cluster;
    uint32_t end_index = index;

    bool has_next_entry =
        fat32_directory_advance_entry(volume, &end_cluster, &end_index);

    fat32_directory_entry_t original_end_entry;
    bool end_entry_modified = false;

    if (after_end_marker && has_next_entry) {
        if (!fat32_directory_read_entry(volume, end_cluster, end_index,
                                        &original_end_entry)) {
            return false;
        }
    }

    // Prepare all LFN entries before writing anything to disk.
    uint8_t checksum = fat32_lfn_checksum(short_entry->name);
    fat32_long_directory_entry_t long_entries[FAT32_LFN_MAX_ENTRIES];

    for (uint32_t i = 0; i < lfn_count; i++) {
        uint8_t order = (uint8_t)(lfn_count - i);

        if (!fat32_lfn_make_entry(name, name_length, order,
                                 checksum, &long_entries[i])) {
            return false;
        }
    }

    uint32_t written_count = 0;
    uint32_t failed_slot = 0;

    /*
     * Move the end marker before writing the LFN sequence.
     *
     * If this write fails, attempt to restore its original contents
     * because the underlying write may have partially succeeded.
     */
    if (after_end_marker && has_next_entry) {
        fat32_directory_entry_t end_entry;

        memset(&end_entry, 0, sizeof(end_entry));

        if (!fat32_directory_write_entry(volume, end_cluster,
                                         end_index, &end_entry)) {
            fat32_directory_write_entry(volume, end_cluster, end_index,
                                        &original_end_entry);
            return false;
        }

        end_entry_modified = true;
    }

    // Write the LFN entries in descending order.
    for (uint32_t i = 0; i < lfn_count; i++) {
        fat32_directory_entry_t disk_entry;

        memcpy(&disk_entry, &long_entries[i], sizeof(disk_entry));

        if (!fat32_directory_write_entry(volume, clusters[i],
                                         indices[i], &disk_entry)) {
            failed_slot = i;
            goto rollback;
        }

        written_count++;
    }

    // Publish the short-name entry only after all LFN entries succeed.
    if (!fat32_directory_write_entry(volume, clusters[lfn_count],
                                     indices[lfn_count], short_entry)) {
        failed_slot = lfn_count;
        goto rollback;
    }

    return true;

rollback:
    /*
     * Restore the entry whose write failed first.
     *
     * This is necessary even when the write returned false, since
     * the storage device may have partially modified the entry.
     */
    bool rollback_success =
        fat32_directory_write_entry(volume, clusters[failed_slot],
                                    indices[failed_slot],
                                    &originals[failed_slot]);

    // Restore previously written LFN entries in reverse order.
    while (written_count > 0) {
        written_count--;

        if (!fat32_directory_write_entry(volume, clusters[written_count],
                                         indices[written_count],
                                         &originals[written_count])) {
            rollback_success = false;
        }
    }

    // Restore the entry following the allocated sequence, if modified.
    if (end_entry_modified) {
        if (!fat32_directory_write_entry(volume, end_cluster, end_index,
                                         &original_end_entry)) {
            rollback_success = false;
        }
    }

    /*
     * The operation failed regardless of whether rollback succeeded.
     * A failed rollback may leave the directory requiring repair.
     */
    (void)rollback_success;

    return false;
}



/*
 * Checks whether an 11-byte FAT32 short name already exists
 * in a directory, including aliases belonging to LFN files.
 *
 * Returns FOUND when a matching entry exists, NOT_FOUND when
 * the complete directory has been searched without a match,
 * or ERROR if validation, reading or FAT traversal fails.
 */
fat32_short_name_status_t fat32_directory_short_name_exists(
    const fat32_volume_t *volume,
    uint32_t directory_cluster,
    const uint8_t name[FAT32_SHORT_NAME_LENGTH])
{
    if (volume == NULL || name == NULL ||
        directory_cluster < FAT32_FIRST_DATA_CLUSTER) {
        return FAT32_SHORT_NAME_ERROR;
    }

    uint32_t entries_per_cluster =
        volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (entries_per_cluster == 0) {
        return FAT32_SHORT_NAME_ERROR;
    }

    uint32_t cluster = directory_cluster;

    // Limit traversal to the volume's cluster count to avoid loops.
    for (uint32_t visited = 0; visited < volume->cluster_count; visited++) {
        for (uint32_t index = 0; index < entries_per_cluster; index++) {
            fat32_directory_entry_t entry;

            if (!fat32_directory_read_entry(volume, cluster, index, &entry)) {
                return FAT32_SHORT_NAME_ERROR;
            }

            uint8_t marker = entry.name[0];

            if (marker == FAT32_DIR_ENTRY_END) {
                return FAT32_SHORT_NAME_NOT_FOUND;
            }

            if (marker == FAT32_DIR_ENTRY_DELETED ||
                entry.attributes == FAT32_ATTR_LONG_NAME) {
                continue;
            }

            if (memcmp(entry.name, name, FAT32_SHORT_NAME_LENGTH) == 0) {
                return FAT32_SHORT_NAME_FOUND;
            }
        }

        uint32_t next_cluster;
        fat32_chain_status_t status =
            fat32_next_cluster(volume, cluster, &next_cluster);

        if (status == FAT32_CHAIN_END) {
            return FAT32_SHORT_NAME_NOT_FOUND;
        }

        if (status == FAT32_CHAIN_ERROR) {
            return FAT32_SHORT_NAME_ERROR;
        }

        cluster = next_cluster;
    }

    // Reaching the traversal limit suggests a malformed cluster chain.
    return FAT32_SHORT_NAME_ERROR;
}


/*
 * Finds the first available numbered FAT32 short-name alias.
 *
 * Candidates are generated in sequence, starting at ~1.
 * An existing alias causes the next candidate to be tried.
 * Any directory read or FAT traversal error aborts selection.
 *
 * Returns false if the filename is invalid, the directory cannot
 * be searched, or all supported sequence numbers are exhausted.
 */
bool fat32_directory_find_short_alias(
    const fat32_volume_t *volume,
    uint32_t directory_cluster,
    const char *filename,
    uint8_t alias[FAT32_SHORT_NAME_LENGTH])
{
    if (volume == NULL || filename == NULL || alias == NULL ||
        directory_cluster < FAT32_FIRST_DATA_CLUSTER) {
        return false;
    }

    const uint32_t max_sequence = 999999;

    for (uint32_t sequence = 1; sequence <= max_sequence; sequence++) {
        uint8_t candidate[FAT32_SHORT_NAME_LENGTH];

        if (!fat32_make_short_alias(filename, sequence, candidate)) {
            return false;
        }

        fat32_short_name_status_t status =
            fat32_directory_short_name_exists(
                volume, directory_cluster, candidate);

        if (status == FAT32_SHORT_NAME_ERROR) {
            return false;
        }

        if (status == FAT32_SHORT_NAME_NOT_FOUND) {
            memcpy(alias, candidate, FAT32_SHORT_NAME_LENGTH);
            return true;
        }

        // This candidate is occupied; try the next sequence number.
    }

    return false;
}


/*
 * Locates the first LFN entry associated with a resolved directory record.
 *
 * Returns false for records without LFN entries or on traversal errors.
 */
bool fat32_directory_find_lfn_start(const fat32_volume_t *volume, uint32_t directory_cluster,
                                   const fat32_directory_record_t *record,
                                   uint32_t *start_cluster, uint32_t *start_index)
{
    if (volume == NULL || record == NULL || start_cluster == NULL || start_index == NULL ||
        directory_cluster < FAT32_FIRST_DATA_CLUSTER || record->lfn_entry_count == 0 ||
        record->lfn_entry_count > FAT32_LFN_MAX_ENTRIES) {
        return false;
    }

    uint32_t entries_per_cluster = volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (entries_per_cluster == 0 || record->directory_index >= entries_per_cluster ||
        volume->cluster_count == 0) {
        return false;
    }

    // Retain the most recent 20 directory-entry positions.
    uint32_t history_clusters[FAT32_LFN_MAX_ENTRIES];
    uint32_t history_indices[FAT32_LFN_MAX_ENTRIES];
    uint32_t positions_seen = 0;

    uint32_t current_cluster = directory_cluster;

    for (uint32_t clusters_visited = 0; clusters_visited < volume->cluster_count;
         clusters_visited++) {
        for (uint32_t index = 0; index < entries_per_cluster; index++) {
            // The history now contains the entries immediately before the short entry.
            if (current_cluster == record->directory_cluster &&
                index == record->directory_index) {
                if (positions_seen < record->lfn_entry_count) {
                    return false;
                }

                uint32_t oldest = (positions_seen - record->lfn_entry_count) %
                                  FAT32_LFN_MAX_ENTRIES;

                *start_cluster = history_clusters[oldest];
                *start_index = history_indices[oldest];

                return true;
            }

            uint32_t slot = positions_seen % FAT32_LFN_MAX_ENTRIES;

            history_clusters[slot] = current_cluster;
            history_indices[slot] = index;
            positions_seen++;
        }

        uint32_t next_cluster;
        fat32_chain_status_t status = fat32_next_cluster(volume, current_cluster, &next_cluster);

        if (status != FAT32_CHAIN_NEXT) {
            return false;
        }

        current_cluster = next_cluster;
    }

    return false;
}

/*
 * Validates the LFN entries associated with a directory record.
 *
 * Confirms that the short entry is unchanged and that each preceding
 * LFN entry has the expected order, attributes and checksum.
 */
bool fat32_directory_validate_lfn(const fat32_volume_t *volume, uint32_t directory_cluster,
                                  const fat32_directory_record_t *record)
{
    if (volume == NULL || record == NULL || record->lfn_entry_count == 0 ||
        record->lfn_entry_count > FAT32_LFN_MAX_ENTRIES) {
        return false;
    }

    uint32_t start_cluster;
    uint32_t start_index;

    if (!fat32_directory_find_lfn_start(volume, directory_cluster, record,
                                        &start_cluster, &start_index)) {
        return false;
    }

    // Ensure the short entry has not changed since the original lookup.
    fat32_directory_entry_t current_short;

    if (!fat32_directory_read_entry(volume, record->directory_cluster,
                                   record->directory_index, &current_short)) {
        return false;
    }

    if (memcmp(&current_short, &record->entry, sizeof(current_short)) != 0) {
        return false;
    }

    uint8_t checksum = fat32_lfn_checksum(record->entry.name);
    uint32_t current_cluster = start_cluster;
    uint32_t current_index = start_index;
    uint32_t entries_per_cluster = volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (entries_per_cluster == 0) {
        return false;
    }

    // Reconstruct the filename independently from the current on-disk entries.
    fat32_lfn_state_t lfn_state;
    memset(&lfn_state, 0, sizeof(lfn_state));

    for (uint32_t i = 0; i < record->lfn_entry_count; i++) {
        fat32_long_directory_entry_t entry;

        if (!fat32_directory_read_entry(volume, current_cluster, current_index,
                                        (fat32_directory_entry_t *)&entry)) {
            return false;
        }

        uint8_t expected_order = (uint8_t)(record->lfn_entry_count - i);

        if (i == 0) {
            expected_order |= FAT32_LFN_LAST_ENTRY;
        }

        if (entry.order != expected_order || entry.attributes != FAT32_ATTR_LONG_NAME ||
            entry.checksum != checksum || entry.type != 0 || entry.first_cluster_low != 0) {
            return false;
        }

        // Construct the filename.
        fat32_lfn_process(&lfn_state, &entry);

        if (!lfn_state.active) {
            return false;
        }

        current_index++;

        if (current_index == entries_per_cluster && i + 1 < record->lfn_entry_count) {
            uint32_t next_cluster;

            if (fat32_next_cluster(volume, current_cluster, &next_cluster) !=
                FAT32_CHAIN_NEXT) {
                return false;
            }

            current_cluster = next_cluster;
            current_index = 0;
        }
    }

    // Verify that the reconstructed filename still matches the original lookup.
    char current_name[FAT32_UTF8_NAME_BUFFER_SIZE];

    if (!fat32_format_long_name(&lfn_state, &current_short, current_name)) {
        return false;
    }

    if (strcmp(current_name, record->name) != 0) {
        return false;
    }

    // The validated LFN sequence must end immediately before its short entry.
    if (current_index == entries_per_cluster) {
        uint32_t next_cluster;

        if (fat32_next_cluster(volume, current_cluster, &next_cluster) != FAT32_CHAIN_NEXT) {
            return false;
        }

        current_cluster = next_cluster;
        current_index = 0;
    }

    return current_cluster == record->directory_cluster &&
           current_index == record->directory_index;
}

/*
 * Deletes the validated LFN entries associated with a directory record.
 *
 * Saves the original entries before modifying them so a failed write
 * can trigger a best-effort rollback.
 */
bool fat32_directory_delete_lfn(const fat32_volume_t *volume, uint32_t directory_cluster,
                                const fat32_directory_record_t *record)
{
    if (volume == NULL || record == NULL || record->lfn_entry_count == 0 ||
        record->lfn_entry_count > FAT32_LFN_MAX_ENTRIES) {
        return false;
    }

    // Validate the complete sequence before making any modifications.
    if (!fat32_directory_validate_lfn(volume, directory_cluster, record)) {
        return false;
    }

    uint32_t current_cluster;
    uint32_t current_index;

    if (!fat32_directory_find_lfn_start(volume, directory_cluster, record,
                                        &current_cluster, &current_index)) {
        return false;
    }

    uint32_t entries_per_cluster = volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (entries_per_cluster == 0) {
        return false;
    }

    // Save all original entries and their physical locations.
    fat32_directory_entry_t originals[FAT32_LFN_MAX_ENTRIES];
    uint32_t clusters[FAT32_LFN_MAX_ENTRIES];
    uint32_t indices[FAT32_LFN_MAX_ENTRIES];

    for (uint32_t i = 0; i < record->lfn_entry_count; i++) {
        clusters[i] = current_cluster;
        indices[i] = current_index;

        if (!fat32_directory_read_entry(volume, current_cluster, current_index, &originals[i])) {
            return false;
        }

        current_index++;

        if (current_index == entries_per_cluster && i + 1 < record->lfn_entry_count) {
            uint32_t next_cluster;

            if (fat32_next_cluster(volume, current_cluster, &next_cluster) != FAT32_CHAIN_NEXT) {
                return false;
            }

            current_cluster = next_cluster;
            current_index = 0;
        }
    }

    // Mark each LFN entry deleted, retaining originals for rollback.
    for (uint32_t i = 0; i < record->lfn_entry_count; i++) {
        fat32_directory_entry_t deleted = originals[i];
        deleted.name[0] = FAT32_DIR_ENTRY_DELETED;

        if (!fat32_directory_write_entry(volume, clusters[i], indices[i], &deleted)) {
            // Restore all entries that were successfully modified.
            for (uint32_t j = 0; j < i; j++) {
                fat32_directory_write_entry(volume, clusters[j], indices[j], &originals[j]);
            }

            return false;
        }
    }

    return true;
}

/*
 * Restores the LFN directory entries associated with a previously
 * resolved record after an interrupted deletion.
 *
 * Reconstructs the entries from the original filename and short-entry
 * checksum. Currently supports filenames accepted by the ASCII-only
 * FAT32 filename encoder.
 */
bool fat32_directory_restore_lfn(const fat32_volume_t *volume, uint32_t directory_cluster,
                                 const fat32_directory_record_t *record)
{
    if (volume == NULL || record == NULL || record->lfn_entry_count == 0 ||
        record->lfn_entry_count > FAT32_LFN_MAX_ENTRIES) {
        return false;
    }

    uint16_t name[FAT32_LFN_MAX_LENGTH];
    uint32_t name_length;

    if (!fat32_filename_to_utf16(record->name, name, &name_length)) {
        return false;
    }

    uint32_t expected_count =
        (name_length + FAT32_LFN_CHARS_PER_ENTRY - 1) / FAT32_LFN_CHARS_PER_ENTRY;

    if (expected_count != record->lfn_entry_count) {
        return false;
    }

    uint32_t current_cluster;
    uint32_t current_index;

    if (!fat32_directory_find_lfn_start(volume, directory_cluster, record,
                                        &current_cluster, &current_index)) {
        return false;
    }

    uint32_t entries_per_cluster = volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

    if (entries_per_cluster == 0) {
        return false;
    }

    uint8_t checksum = fat32_lfn_checksum(record->entry.name);

    for (uint32_t i = 0; i < record->lfn_entry_count; i++) {
        uint8_t order = (uint8_t)(record->lfn_entry_count - i);
        fat32_long_directory_entry_t entry;

        if (!fat32_lfn_make_entry(name, name_length, order, checksum, &entry)) {
            return false;
        }

        if (!fat32_directory_write_entry(volume, current_cluster, current_index,
                                         (const fat32_directory_entry_t *)&entry)) {
            return false;
        }

        current_index++;

        if (current_index == entries_per_cluster && i + 1 < record->lfn_entry_count) {
            uint32_t next_cluster;

            if (fat32_next_cluster(volume, current_cluster, &next_cluster) != FAT32_CHAIN_NEXT) {
                return false;
            }

            current_cluster = next_cluster;
            current_index = 0;
        }
    }

    return true;
}
