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
#include <vfs.h>
#include <fat32.h>
#include <string.h>

/*
 * Creates a directory on a FAT32 volume.
 */
static os_error_t fat32_vfs_mkdir(void *context, const char *path)
{
    if (context == NULL || path == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const fat32_volume_t *volume = (const fat32_volume_t *)context;
    fat32_directory_record_t record;

    fat32_lookup_status_t status = fat32_path_resolve(volume, path, &record);

    if (status == FAT32_LOOKUP_FOUND) {
        return OS_ERR_ALREADY_EXISTS;
    }

    if (status == FAT32_LOOKUP_NOT_DIRECTORY) {
        return OS_ERR_NOT_DIRECTORY;
    }

    if (status == FAT32_LOOKUP_ERROR) {
        return OS_ERR_IO;
    }

    // Only attempt creation when the target does not exist.
    if (!fat32_directory_create(volume, path)) {
        return OS_ERR_IO;
    }

    return OS_OK;
}

/*
 * Removes an empty directory from a mounted FAT32 volume.
 */
static os_error_t fat32_vfs_rmdir(void *context, const char *path)
{
    if (context == NULL || path == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const fat32_volume_t *volume = (const fat32_volume_t *)context;

    if (!fat32_directory_delete(volume, path)) {
        return OS_ERR_IO;
    }

    return OS_OK;
}

/*
 * Enumerates files and directories on a FAT32 volume.
 *
 * Resolves the directory path, opens its cluster iterator and
 * passes each entry to the VFS callback.
 *
 * Enumeration stops on completion, a filesystem error or a
 * non-success result returned by the callback.
 */
static os_error_t fat32_vfs_listdir(void *context, const char *path,
                                   os_error_t (*callback)(const vfs_dirent_t *entry, void *context),
                                   void *callback_context)
{
    if (context == NULL || path == NULL || callback == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const fat32_volume_t *volume = (const fat32_volume_t *)context;
    fat32_directory_record_t directory;

    fat32_lookup_status_t lookup = fat32_path_resolve(volume, path, &directory);

    if (lookup == FAT32_LOOKUP_NOT_FOUND) {
        return OS_ERR_NOT_FOUND;
    }

    if (lookup == FAT32_LOOKUP_NOT_DIRECTORY) {
        return OS_ERR_NOT_DIRECTORY;
    }

    if (lookup != FAT32_LOOKUP_FOUND) {
        return OS_ERR_IO;
    }

    if (!(directory.entry.attributes & FAT32_ATTR_DIRECTORY)) {
        return OS_ERR_NOT_DIRECTORY;
    }

    uint32_t cluster = fat32_get_first_cluster(&directory.entry);
    fat32_directory_iterator_t iterator;

    if (!fat32_directory_open(volume, cluster, &iterator)) {
        return OS_ERR_IO;
    }

    os_error_t result = OS_OK;
    fat32_directory_record_t record;

    while (true) {
        fat32_directory_status_t status = fat32_directory_next(&iterator, &record);

        if (status == FAT32_DIR_END) {
            break;
        }

        if (status == FAT32_DIR_ERROR) {
            result = OS_ERR_IO;
            break;
        }

        vfs_dirent_t entry;

        memset(&entry, 0, sizeof(entry));

        size_t length = strlen(record.name);

        if (length >= sizeof(entry.name)) {
            result = OS_ERR_NAME_TOO_LONG;
            break;
        }

        memcpy(entry.name, record.name, length + 1);

        entry.size = record.entry.file_size;
        entry.is_directory = (record.entry.attributes & FAT32_ATTR_DIRECTORY) != 0;

        result = callback(&entry, callback_context);

        if (result != OS_OK) {
            break;
        }
    }

    fat32_directory_close(&iterator);

    return result;
}

const vfs_driver_t fat32_vfs_driver = {
    .name = "FAT32",
    .mkdir = fat32_vfs_mkdir,
    .rmdir = fat32_vfs_rmdir,
    .listdir = fat32_vfs_listdir
};
