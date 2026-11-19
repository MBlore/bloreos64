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
#include <mem.h>

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

        fat32_timestamp_decode(&record.entry, &entry.created, &entry.modified, &entry.accessed);

        result = callback(&entry, callback_context);

        if (result != OS_OK) {
            break;
        }
    }

    fat32_directory_close(&iterator);

    return result;
}


/*
 * Retrieves metadata for a FAT32 file or directory.
 *
 * Converts FAT32 directory-entry information into the
 * filesystem-independent VFS stat structure.
 */
static os_error_t fat32_vfs_stat(void *context, const char *path, vfs_stat_t *stat)
{
    if (context == NULL || path == NULL || stat == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const fat32_volume_t *volume = (const fat32_volume_t *)context;
    fat32_directory_record_t record;

    fat32_lookup_status_t result = fat32_path_resolve(volume, path, &record);

    if (result == FAT32_LOOKUP_NOT_FOUND) {
        return OS_ERR_NOT_FOUND;
    }

    if (result == FAT32_LOOKUP_NOT_DIRECTORY) {
        return OS_ERR_NOT_DIRECTORY;
    }

    if (result != FAT32_LOOKUP_FOUND) {
        return OS_ERR_IO;
    }

    stat->type = (record.entry.attributes & FAT32_ATTR_DIRECTORY)
                 ? VFS_TYPE_DIRECTORY : VFS_TYPE_FILE;

    stat->size = record.entry.file_size;
    stat->attributes = record.entry.attributes;
    
    fat32_timestamp_decode(&record.entry, &stat->created, &stat->modified, &stat->accessed);

    return OS_OK;
}

/*
 * Opens or creates a regular FAT32 file.
 *
 * Allocates a private FAT32 file handle and returns it to the VFS.
 * Existing files are preserved when VFS_OPEN_CREATE is specified.
 */
static os_error_t fat32_vfs_open(void *context, const char *path, uint32_t flags, void **handle)
{
    if (context == NULL || path == NULL || handle == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    *handle = NULL;

    const fat32_volume_t *volume = (const fat32_volume_t *)context;
    fat32_directory_record_t record;

    fat32_lookup_status_t status = fat32_path_resolve(volume, path, &record);

    if (status == FAT32_LOOKUP_NOT_DIRECTORY) {
        return OS_ERR_NOT_DIRECTORY;
    }

    if (status == FAT32_LOOKUP_ERROR) {
        return OS_ERR_IO;
    }

    if (status == FAT32_LOOKUP_FOUND) {
        if (record.entry.attributes & FAT32_ATTR_DIRECTORY) {
            return OS_ERR_IS_DIRECTORY;
        }

        if ((flags & VFS_OPEN_WRITE) && (record.entry.attributes & FAT32_ATTR_READ_ONLY)) {
            return OS_ERR_READ_ONLY;
        }
    } else {
        if (!(flags & VFS_OPEN_CREATE)) {
            return OS_ERR_NOT_FOUND;
        }

        if (!fat32_file_create(volume, path)) {
            return OS_ERR_IO;
        }
    }

    fat32_file_t *file = kalloc(sizeof(fat32_file_t));

    if (file == NULL) {
        return OS_ERR_OUT_OF_MEMORY;
    }

    if (!fat32_file_open(volume, path, file)) {
        kfree(file);
        return OS_ERR_IO;
    }

    *handle = file;

    return OS_OK;
}

/*
 * Reads data from an open FAT32 file.
 */
static int64_t fat32_vfs_read(void *handle, void *buffer, size_t count)
{
    return fat32_file_read((fat32_file_t *)handle, buffer, count);
}

/*
 * Writes data to an open FAT32 file.
 */
static int64_t fat32_vfs_write(void *handle, const void *buffer, size_t count)
{
    return fat32_file_write((fat32_file_t *)handle, buffer, count);
}

/*
 * Changes the position of an open FAT32 file.
 *
 * The VFS seek modes deliberately correspond to the FAT32 seek modes.
 */
static int64_t fat32_vfs_seek(void *handle, int64_t offset, vfs_seek_mode_t mode)
{
    fat32_seek_mode_t fat32_mode;

    switch (mode) {
        case VFS_SEEK_SET:
            fat32_mode = FAT32_SEEK_SET;
            break;

        case VFS_SEEK_CUR:
            fat32_mode = FAT32_SEEK_CUR;
            break;

        case VFS_SEEK_END:
            fat32_mode = FAT32_SEEK_END;
            break;

        default:
            return -1;
    }

    return fat32_file_seek((fat32_file_t *)handle, offset, fat32_mode);
}

/*
 * Closes and releases a FAT32 file handle.
 */
static void fat32_vfs_close(void *handle)
{
    if (handle == NULL) {
        return;
    }

    fat32_file_t *file = (fat32_file_t *)handle;

    fat32_file_close(file);
    kfree(file);
}

/*
 * Deletes a regular FAT32 file.
 *
 * Rejects directories and preserves lookup errors before invoking
 * the underlying FAT32 deletion implementation.
 */
static os_error_t fat32_vfs_unlink(void *context, const char *path)
{
    if (context == NULL || path == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const fat32_volume_t *volume = (const fat32_volume_t *)context;
    fat32_directory_record_t record;

    fat32_lookup_status_t status = fat32_path_resolve(volume, path, &record);

    if (status == FAT32_LOOKUP_NOT_FOUND) {
        return OS_ERR_NOT_FOUND;
    }

    if (status == FAT32_LOOKUP_NOT_DIRECTORY) {
        return OS_ERR_NOT_DIRECTORY;
    }

    if (status != FAT32_LOOKUP_FOUND) {
        return OS_ERR_IO;
    }

    if (record.entry.attributes & FAT32_ATTR_DIRECTORY) {
        return OS_ERR_IS_DIRECTORY;
    }

    if (record.entry.attributes & FAT32_ATTR_READ_ONLY) {
        return OS_ERR_READ_ONLY;
    }

    if (!fat32_file_delete(volume, path)) {
        return OS_ERR_IO;
    }

    return OS_OK;
}

const vfs_driver_t fat32_vfs_driver = {
    .name = "FAT32",
    .mkdir = fat32_vfs_mkdir,
    .rmdir = fat32_vfs_rmdir,
    .listdir = fat32_vfs_listdir,
    .stat = fat32_vfs_stat,
    .open = fat32_vfs_open,
    .read = fat32_vfs_read,
    .write = fat32_vfs_write,
    .seek = fat32_vfs_seek,
    .close = fat32_vfs_close,
    .unlink = fat32_vfs_unlink
};