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
 * Opens an existing file for reading.
 *
 * Resolves the absolute path and initialises an independent file handle.
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
    file->first_cluster = first_cluster;
    file->current_cluster = first_cluster;
    file->cluster_index = 0;
    file->size = record.entry.file_size;
    file->position = 0;
    file->buffer_valid = false;

    return true;
}