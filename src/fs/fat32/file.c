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
 * Opens an existing regular file.
 *
 * Resolves the absolute path and initialises an independent file handle,
 * retaining the physical location of the file's directory entry.
 *
 * The caller must provide an unopened file handle and ensure the volume
 * remains valid until the file is closed.
 *
 * Returns true on success, or false if the file cannot be opened.
 */
bool fat32_file_open(const fat32_volume_t *volume, const char *path, fat32_file_t *file)
{
    if (volume == NULL || path == NULL || file == NULL) {
        return false;
    }

    memset(file, 0, sizeof(fat32_file_t));

    fat32_directory_record_t record;

    if (fat32_path_resolve(volume, path, &record) != FAT32_LOOKUP_FOUND) {
        return false;
    }

    // Directories cannot be opened as regular files.
    if (record.entry.attributes & FAT32_ATTR_DIRECTORY) {
        return false;
    }

    uint32_t first_cluster = fat32_get_first_cluster(&record.entry);

    // Non-empty files must reference a valid data cluster.
    if (record.entry.file_size > 0) {
        uint64_t lba;

        if (!fat32_cluster_to_lba(volume, first_cluster, &lba)) {
            return false;
        }

        file->buffer = kalloc(volume->bytes_per_cluster);

        if (file->buffer == NULL) {
            return false;
        }
    }

    file->volume = volume;
    file->directory_cluster = record.directory_cluster;
    file->directory_index = record.directory_index;
    file->first_cluster = first_cluster;
    file->current_cluster = first_cluster;
    file->cluster_index = 0;
    file->size = record.entry.file_size;
    file->position = 0;
    file->attributes = record.entry.attributes;
    file->buffer_valid = false;

    return true;
}

/*
 * Closes an open file and releases its allocated resources.
 *
 * The file handle is reset to an unopened state so it can be reused.
 */
void fat32_file_close(fat32_file_t *file)
{
    if (file == NULL) {
        return;
    }

    if (file->buffer != NULL) {
        kfree(file->buffer);
    }

    memset(file, 0, sizeof(fat32_file_t));
}

/*
 * Loads the requested logical file cluster into the cache.
 */
static bool fat32_file_load_cluster(fat32_file_t *file, uint32_t cluster_index)
{
    if (cluster_index >= file->volume->cluster_count) {
        return false;
    }

    // Restart traversal when accessing an earlier cluster.
    if (cluster_index < file->cluster_index) {
        file->current_cluster = file->first_cluster;
        file->cluster_index = 0;
        file->buffer_valid = false;
    }

    // Traverse the FAT chain until the requested cluster is reached.
    while (file->cluster_index < cluster_index) {
        uint32_t next_cluster;

        if (fat32_next_cluster(file->volume, file->current_cluster,
                               &next_cluster) != FAT32_CHAIN_NEXT) {
            return false;
        }

        file->current_cluster = next_cluster;
        file->cluster_index++;
        file->buffer_valid = false;
    }

    // Only perform disk I/O when the cached cluster is invalid.
    if (!file->buffer_valid) {
        if (!fat32_read_cluster(file->volume, file->current_cluster, file->buffer)) {
            return false;
        }

        file->buffer_valid = true;
    }

    return true;
}

/*
 * Reads up to count bytes from the current file position.
 *
 * Returns the number of bytes read, zero at EOF, or -1 on error
 * if no bytes have been successfully read.
 */
int64_t fat32_file_read(fat32_file_t *file, void *buffer, size_t count)
{
    if (file == NULL || file->volume == NULL) {
        return -1;
    }

    if (count == 0) {
        return 0;
    }

    if (buffer == NULL) {
        return -1;
    }

    if (file->position >= file->size) {
        return 0;
    }

    // Prevent reads beyond the end of the file.
    uint64_t remaining = file->size - file->position;

    if (count > remaining) {
        count = remaining;
    }

    uint8_t *destination = buffer;
    size_t bytes_read = 0;
    uint32_t cluster_size = file->volume->bytes_per_cluster;

    while (bytes_read < count) {
        uint32_t cluster_index = file->position / cluster_size;
        uint32_t cluster_offset = file->position % cluster_size;

        if (!fat32_file_load_cluster(file, cluster_index)) {
            return bytes_read > 0 ? (int64_t)bytes_read : -1;
        }

        // Limit the copy to the remaining bytes in this cluster.
        size_t chunk_size = cluster_size - cluster_offset;

        if (chunk_size > count - bytes_read) {
            chunk_size = count - bytes_read;
        }

        memcpy(destination + bytes_read, file->buffer + cluster_offset, chunk_size);

        bytes_read += chunk_size;
        file->position += chunk_size;
    }

    return (int64_t)bytes_read;
}

/*
 * Changes the logical position within an open file.
 *
 * Seeking beyond the end of the file is permitted, but seeking
 * before the beginning of the file is not.
 *
 * Returns the new file position, or -1 on error.
 * The existing file position remains unchanged on failure.
 */
int64_t fat32_file_seek(fat32_file_t *file, int64_t offset, fat32_seek_mode_t mode)
{
    if (file == NULL || file->volume == NULL) {
        return -1;
    }

    int64_t base;

    switch (mode) {
        case FAT32_SEEK_SET:
            base = 0;
            break;

        case FAT32_SEEK_CUR:
            base = (int64_t)file->position;
            break;

        case FAT32_SEEK_END:
            base = (int64_t)file->size;
            break;

        default:
            return -1;
    }

    // Prevent signed integer overflow.
    if (offset > 0 && base > INT64_MAX - offset) {
        return -1;
    }

    // Prevent seeking before the beginning of the file.
    if (offset < -base) {
        return -1;
    }

    int64_t position = base + offset;

    file->position = (uint64_t)position;

    return position;
}

/*
 * Writes data at the current file position, extending the file
 * and its cluster chain when necessary.
 *
 * Returns the number of bytes written, or -1 on failure.
 * Writes starting beyond the end of the file are not yet supported.
 */
int64_t fat32_file_write(fat32_file_t *file, const void *buffer, size_t count)
{
    if (file == NULL || file->volume == NULL) {
        return -1;
    }

    if (file->attributes & FAT32_ATTR_READ_ONLY) {
        return -1;
    }

    if (count == 0) {
        return 0;
    }

    if (buffer == NULL || file->position > file->size) {
        return -1;
    }

    // Prevent writes exceeding the maximum FAT32 file size.
    if (file->position > UINT32_MAX || count > UINT32_MAX - file->position) {
        return -1;
    }

    uint64_t end_position = file->position + count;

    // Expand the cluster chain if the write extends the file.
    if (end_position > file->size) {
        if (!fat32_file_ensure_capacity(file, end_position)) {
            return -1;
        }
    }

    const uint8_t *source = buffer;
    size_t bytes_written = 0;
    uint32_t cluster_size = file->volume->bytes_per_cluster;

    while (bytes_written < count) {
        uint32_t cluster_index = file->position / cluster_size;
        uint32_t cluster_offset = file->position % cluster_size;

        if (!fat32_file_load_cluster(file, cluster_index)) {
            break;
        }

        size_t chunk_size = cluster_size - cluster_offset;

        if (chunk_size > count - bytes_written) {
            chunk_size = count - bytes_written;
        }

        memcpy(file->buffer + cluster_offset, source + bytes_written, chunk_size);

        if (!fat32_write_cluster(file->volume, file->current_cluster, file->buffer)) {
            file->buffer_valid = false;
            break;
        }

        bytes_written += chunk_size;
        file->position += chunk_size;

        // Increase the logical file size as data is written.
        if (file->position > file->size) {
            file->size = file->position;
        }
    }

    if (bytes_written == 0) {
        return -1;
    }

    // Persist the updated file size, first cluster and archive attribute.
    if (!fat32_file_update_metadata(file)) {
        return -1;
    }

    return (int64_t)bytes_written;
}

/*
 * Synchronises an open file's metadata with its on-disk directory entry.
 *
 * Updates the file size, first cluster number and archive attribute,
 * preserving all other directory entry fields.
 *
 * Returns true on success, or false on validation or I/O failure.
 */
bool fat32_file_update_metadata(fat32_file_t *file)
{
    if (file == NULL || file->volume == NULL) {
        return false;
    }

    // FAT32 stores file sizes using an unsigned 32-bit integer.
    if (file->size > UINT32_MAX) {
        return false;
    }

    if (file->attributes & FAT32_ATTR_READ_ONLY) {
        return false;
    }

    fat32_directory_entry_t entry;

    if (!fat32_directory_read_entry(file->volume, file->directory_cluster,
                                    file->directory_index, &entry)) {
        return false;
    }

    // Ensure the entry represents an existing writable regular file.
    if (entry.name[0] == FAT32_DIR_ENTRY_END ||
        entry.name[0] == FAT32_DIR_ENTRY_DELETED ||
        entry.attributes & (FAT32_ATTR_DIRECTORY | FAT32_ATTR_VOLUME_ID |
                            FAT32_ATTR_READ_ONLY)) {
        return false;
    }

    // Update the first cluster number and file size.
    entry.first_cluster_high = (uint16_t)(file->first_cluster >> 16);
    entry.first_cluster_low = (uint16_t)file->first_cluster;
    entry.file_size = (uint32_t)file->size;

    // Indicate that the file has been modified.
    entry.attributes |= FAT32_ATTR_ARCHIVE;

    if (!fat32_directory_write_entry(file->volume, file->directory_cluster,
                                     file->directory_index, &entry)) {
        return false;
    }

    file->attributes = entry.attributes;

    return true;
}

/*
 * Ensures that a file's cluster chain can accommodate the requested
 * capacity in bytes.
 *
 * Allocates additional clusters when necessary, including the first
 * cluster of an empty file.
 *
 * Does not modify the logical file size or persist directory metadata.
 * Clusters allocated before a failure may remain allocated.
 */
bool fat32_file_ensure_capacity(fat32_file_t *file, uint64_t capacity)
{
    if (file == NULL || file->volume == NULL || capacity > UINT32_MAX) {
        return false;
    }

    if (file->attributes & FAT32_ATTR_READ_ONLY) {
        return false;
    }

    if (capacity == 0) {
        return true;
    }

    const fat32_volume_t *volume = file->volume;
    uint32_t cluster_size = volume->bytes_per_cluster;
    uint32_t required = (capacity + cluster_size - 1) / cluster_size;

    if (required > volume->cluster_count) {
        return false;
    }

    // Allocate a cache buffer if the file was opened empty.
    if (file->buffer == NULL) {
        file->buffer = kalloc(cluster_size);

        if (file->buffer == NULL) {
            return false;
        }

        file->buffer_valid = false;
    }

    // Allocate the first cluster if the file has no cluster chain.
    if (file->first_cluster == 0) {
        uint32_t cluster;

        if (!fat32_allocate_cluster(volume, &cluster)) {
            return false;
        }

        file->first_cluster = cluster;
        file->current_cluster = cluster;
        file->cluster_index = 0;
        file->buffer_valid = false;
    }

    uint32_t current = file->first_cluster;
    uint32_t allocated = 1;

    // Traverse existing clusters before allocating additional storage.
    while (allocated < required) {
        uint32_t next;
        fat32_chain_status_t status = fat32_next_cluster(volume, current, &next);

        if (status == FAT32_CHAIN_ERROR) {
            return false;
        }

        if (status == FAT32_CHAIN_END) {
            if (!fat32_extend_chain(volume, current, &next)) {
                return false;
            }
        }

        current = next;
        allocated++;
    }

    return true;
}



/*
 * Creates an empty regular file in the FAT32 root directory.
 *
 * The file initially has no allocated data clusters.
 */
bool fat32_file_create(const fat32_volume_t *volume, const char *path)
{
    if (volume == NULL || path == NULL || path[0] != '/') {
        return false;
    }

    // Creation in nested directories is not yet supported.
    const char *filename = path + 1;

    if (*filename == '\0') {
        return false;
    }

    uint16_t long_name[FAT32_LFN_MAX_LENGTH];
    uint32_t name_length;

    if (!fat32_filename_to_utf16(filename, long_name, &name_length)) {
        return false;
    }

    // Reject an existing file or an error while searching the directory.
    fat32_directory_record_t existing;

    if (fat32_directory_find(volume, volume->root_cluster, filename, &existing) !=
        FAT32_LOOKUP_NOT_FOUND) {
        return false;
    }

    uint8_t short_name[FAT32_SHORT_NAME_LENGTH];
    bool is_short_name = fat32_make_short_name(filename, short_name);

    if (is_short_name) {
        // An 8.3 name must not collide with an existing short alias.
        if (fat32_directory_short_name_exists(volume, volume->root_cluster, short_name) !=
            FAT32_SHORT_NAME_NOT_FOUND) {
            return false;
        }
    } else {
        if (!fat32_directory_find_short_alias(volume, volume->root_cluster,
                                               filename, short_name)) {
            return false;
        }
    }

    // Prepare the short entry shared by both filename formats.
    fat32_directory_entry_t entry;

    memset(&entry, 0, sizeof(entry));
    memcpy(entry.name, short_name, FAT32_SHORT_NAME_LENGTH);

    entry.attributes = FAT32_ATTR_ARCHIVE;

    if (!is_short_name) {
        uint32_t lfn_entries =
            (name_length + FAT32_LFN_CHARS_PER_ENTRY - 1) / FAT32_LFN_CHARS_PER_ENTRY;

        uint32_t entry_cluster;
        uint32_t entry_index;

        if (!fat32_directory_find_free_entries(volume, volume->root_cluster, lfn_entries + 1,
                                                &entry_cluster, &entry_index)) {
            return false;
        }

        return fat32_directory_write_long_name(volume, entry_cluster, entry_index,
                                               long_name, name_length, &entry);
    }

    // Preserve the existing single-entry creation behaviour for 8.3 names.
    uint32_t entry_cluster;
    uint32_t entry_index;

    if (!fat32_directory_find_free_entry(volume, volume->root_cluster,
                                         &entry_cluster, &entry_index)) {
        return false;
    }

    fat32_directory_entry_t previous;

    if (!fat32_directory_read_entry(volume, entry_cluster, entry_index, &previous)) {
        return false;
    }

    if (previous.name[0] != FAT32_DIR_ENTRY_DELETED &&
        previous.name[0] != FAT32_DIR_ENTRY_END) {
        return false;
    }

    if (previous.name[0] == FAT32_DIR_ENTRY_END) {
        uint32_t entries_per_cluster =
            volume->bytes_per_cluster / FAT32_DIRECTORY_ENTRY_SIZE;

        if (entry_index + 1 < entries_per_cluster) {
            fat32_directory_entry_t end_entry;

            memset(&end_entry, 0, sizeof(end_entry));

            if (!fat32_directory_write_entry(volume, entry_cluster,
                                             entry_index + 1, &end_entry)) {
                return false;
            }
        }
    }

    return fat32_directory_write_entry(volume, entry_cluster, entry_index, &entry);
}


/*
 * Deletes a regular file from the FAT32 root directory.
 *
 * Supports both short 8.3 filenames and validated long filenames.
 * Nested paths, directories and read-only files are not supported.
 *
 * Long filename entries are validated and deleted before the short
 * entry because the LFN deletion helper requires an intact short entry.
 *
 * This operation is not atomic. A write failure may leave orphaned
 * LFN entries or allocated clusters. The directory must not be
 * modified concurrently during deletion.
 */
bool fat32_file_delete(const fat32_volume_t *volume, const char *path)
{
    if (volume == NULL || path == NULL || path[0] != '/') {
        return false;
    }

    // Only files directly inside the root directory are supported.
    const char *filename = path + 1;

    if (*filename == '\0') {
        return false;
    }

    for (const char *cursor = filename; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            return false;
        }
    }

    fat32_directory_record_t record;

    if (fat32_directory_find(volume, volume->root_cluster, filename, &record) !=
        FAT32_LOOKUP_FOUND) {
        return false;
    }

    // Do not delete directories, read-only files or volume labels.
    if (record.entry.attributes &
        (FAT32_ATTR_DIRECTORY | FAT32_ATTR_READ_ONLY | FAT32_ATTR_VOLUME_ID)) {
        return false;
    }

    // For short filenames, ensure the lookup resolved the intended short entry.
    if (record.lfn_entry_count == 0) {
        uint8_t short_name[FAT32_SHORT_NAME_LENGTH];

        if (!fat32_make_short_name(filename, short_name) ||
            memcmp(record.entry.name, short_name, FAT32_SHORT_NAME_LENGTH) != 0) {
            return false;
        }
    } else {
        // Verify the complete LFN sequence before modifying the directory.
        if (!fat32_directory_validate_lfn(volume, volume->root_cluster, &record)) {
            return false;
        }
    }

    uint32_t first_cluster =
        ((uint32_t)record.entry.first_cluster_high << 16) |
        record.entry.first_cluster_low;

    // An empty file may legitimately have no allocated clusters.
    if (first_cluster == 0 && record.entry.file_size != 0) {
        return false;
    }

    // Reject an invalid starting cluster before changing directory entries.
    if (first_cluster != 0 &&
        (first_cluster < FAT32_FIRST_DATA_CLUSTER ||
         first_cluster >= volume->cluster_count + FAT32_FIRST_DATA_CLUSTER)) {
        return false;
    }

    // Remove validated LFN entries while the associated short entry still exists.
    if (record.lfn_entry_count != 0) {
        if (!fat32_directory_delete_lfn(volume, volume->root_cluster, &record)) {
            return false;
        }
    }

    // Mark the short entry deleted so the file is no longer visible.
    fat32_directory_entry_t deleted_entry = record.entry;
    deleted_entry.name[0] = FAT32_DIR_ENTRY_DELETED;

    if (!fat32_directory_write_entry(volume, record.directory_cluster,
                                     record.directory_index, &deleted_entry)) {
        // The short entry remains intact, so restore its associated LFN entries.
        if (record.lfn_entry_count != 0) {
            fat32_directory_restore_lfn(volume, volume->root_cluster, &record);
        }

        return false;
    }

    // Release the file's allocated data clusters.
    if (first_cluster >= FAT32_FIRST_DATA_CLUSTER) {
        if (!fat32_release_chain(volume, first_cluster)) {
            return false;
        }
    }

    return true;
}


