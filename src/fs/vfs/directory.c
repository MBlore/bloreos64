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

/*
 * Creates a directory using the filesystem mounted at the supplied path.
 */
os_error_t vfs_mkdir(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const char *relative_path = NULL;
    const vfs_mount_t *mount = vfs_resolve(path, &relative_path);

    if (mount == NULL) {
        return OS_ERR_NOT_MOUNTED;
    }

    if (mount->driver->mkdir == NULL) {
        return OS_ERR_NOT_SUPPORTED;
    }

    return mount->driver->mkdir(mount->context, relative_path);
}

/*
 * Removes an empty directory using the filesystem mounted at the supplied path.
 */
os_error_t vfs_rmdir(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const char *relative_path = NULL;
    const vfs_mount_t *mount = vfs_resolve(path, &relative_path);

    if (mount == NULL) {
        return OS_ERR_NOT_MOUNTED;
    }

    if (mount->driver->rmdir == NULL) {
        return OS_ERR_NOT_SUPPORTED;
    }

    return mount->driver->rmdir(mount->context, relative_path);
}

/*
 * Enumerates files and directories through the mounted filesystem driver.
 *
 * The callback is invoked for each entry. Enumeration stops if the
 * callback returns an error.
 */
os_error_t vfs_listdir(const char *path,
                       os_error_t (*callback)(const vfs_dirent_t *entry, void *context),
                       void *context)
{
    if (path == NULL || path[0] != '/' || callback == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const char *relative_path = NULL;
    const vfs_mount_t *mount = vfs_resolve(path, &relative_path);

    if (mount == NULL) {
        return OS_ERR_NOT_MOUNTED;
    }

    if (mount->driver->listdir == NULL) {
        return OS_ERR_NOT_SUPPORTED;
    }

    return mount->driver->listdir(mount->context, relative_path, callback, context);
}

/*
 * Retrieves metadata for a file or directory using the filesystem
 * mounted at the supplied path.
 */
os_error_t vfs_stat(const char *path, vfs_stat_t *stat)
{
    if (path == NULL || path[0] != '/' || stat == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const char *relative_path = NULL;
    const vfs_mount_t *mount = vfs_resolve(path, &relative_path);

    if (mount == NULL) {
        return OS_ERR_NOT_MOUNTED;
    }

    if (mount->driver->stat == NULL) {
        return OS_ERR_NOT_SUPPORTED;
    }

    return mount->driver->stat(mount->context, relative_path, stat);
}



