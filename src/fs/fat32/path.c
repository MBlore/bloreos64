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

fat32_lookup_status_t fat32_path_resolve(const fat32_volume_t *volume, const char *path,
                                        fat32_directory_record_t *record)
{
    if (volume == NULL || path == NULL || record == NULL || path[0] != '/') {
        return FAT32_LOOKUP_ERROR;
    }

    uint32_t cluster = volume->root_cluster;
    char name[FAT32_UTF8_NAME_BUFFER_SIZE];

    // Construct a synthetic directory record for the root directory.
    memset(record, 0, sizeof(fat32_directory_record_t));

    record->entry.attributes = FAT32_ATTR_DIRECTORY;
    record->entry.first_cluster_high = (uint16_t)(cluster >> 16);
    record->entry.first_cluster_low = (uint16_t)cluster;
    record->name[0] = '/';

    while (*path == '/') {
        path++;
    }

    while (*path != '\0') {
        size_t length = 0;

        // Extract the next path component.
        while (*path != '\0' && *path != '/') {
            if (length >= sizeof(name) - 1) {
                return FAT32_LOOKUP_ERROR;
            }

            name[length++] = *path++;
        }

        name[length] = '\0';

        // The current directory remains unchanged for ".".
        // The root directory is its own parent.
        if (strcmp(name, ".") != 0 &&
            !(strcmp(name, "..") == 0 && cluster == volume->root_cluster)) {
            fat32_lookup_status_t status = fat32_directory_find(volume, cluster, name, record);

            if (status != FAT32_LOOKUP_FOUND) {
                return status;
            }

            // FAT32 represents a parent pointing to the root using cluster zero.
            if (strcmp(name, "..") == 0 && fat32_get_first_cluster(&record->entry) == 0) {
                record->entry.first_cluster_high = (uint16_t)(volume->root_cluster >> 16);
                record->entry.first_cluster_low = (uint16_t)volume->root_cluster;
                record->name[0] = '/';
                record->name[1] = '\0';
            }
        }

        if (*path == '\0') {
            return FAT32_LOOKUP_FOUND;
        }

        if (!(record->entry.attributes & FAT32_ATTR_DIRECTORY)) {
            return FAT32_LOOKUP_NOT_DIRECTORY;
        }

        while (*path == '/') {
            path++;
        }

        cluster = fat32_get_first_cluster(&record->entry);
    }

    return FAT32_LOOKUP_FOUND;
}