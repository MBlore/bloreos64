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
#include <string.h>

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


/*
 * Opens a directory through the mounted filesystem driver.
 */
os_error_t vfs_opendir(const char *path, vfs_dir_t *dir)
{
    if (path == NULL || path[0] != '/' || dir == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    memset(dir, 0, sizeof(*dir));

    const char *relative_path = NULL;
    const vfs_mount_t *mount = vfs_resolve(path, &relative_path);

    if (mount == NULL) {
        return OS_ERR_NOT_MOUNTED;
    }

    if (mount->driver->opendir == NULL) {
        return OS_ERR_NOT_SUPPORTED;
    }

    void *handle = NULL;

    os_error_t result = mount->driver->opendir(
        mount->context, relative_path, &handle
    );

    if (result != OS_OK) {
        return result;
    }

    dir->driver = mount->driver;
    dir->context = mount->context;
    dir->private_data = handle;

    return OS_OK;
}

/*
 * Reads the next entry from an open directory.
 *
 * On success, has_entry is true when an entry was returned,
 * or false when the directory has reached EOF.
 */
os_error_t vfs_readdir(vfs_dir_t *dir, vfs_dirent_t *entry,
                       bool *has_entry)
{
    if (dir == NULL || entry == NULL || has_entry == NULL ||
        dir->driver == NULL || dir->private_data == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    *has_entry = false;

    if (dir->driver->readdir == NULL) {
        return OS_ERR_NOT_SUPPORTED;
    }

    return dir->driver->readdir(
        dir->private_data, entry, has_entry
    );
}

/*
 * Closes a directory and releases its filesystem-owned resources.
 */
void vfs_closedir(vfs_dir_t *dir)
{
    if (dir == NULL) {
        return;
    }

    if (dir->driver != NULL &&
        dir->driver->closedir != NULL &&
        dir->private_data != NULL) {
        dir->driver->closedir(dir->private_data);
    }

    memset(dir, 0, sizeof(*dir));
}




