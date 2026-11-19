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
#include <str.h>
#include <string.h>

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static size_t mount_count;

/*
 * Initialises the VFS mount registry.
 *
 * Must be called before registering any mounts.
 * Invalidates all previously registered mount entries.
 */
void vfs_init(void)
{
    mount_count = 0;
}

/*
 * Registers a filesystem at an absolute mount path.
 */
os_error_t vfs_mount(const char *path, const vfs_driver_t *driver, void *context)
{
    if (path == NULL || driver == NULL || path[0] != '/') {
        return OS_ERR_INVALID_ARGUMENT;
    }

    size_t length = strlen(path);

    if (length == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    if (length >= VFS_MAX_MOUNT_PATH) {
        return OS_ERR_NAME_TOO_LONG;
    }

    if (length > 1 && path[length - 1] == '/') {
        return OS_ERR_INVALID_ARGUMENT;
    }

    for (size_t i = 0; i < mount_count; i++) {
        if (strcmp(mounts[i].path, path) == 0) {
            return OS_ERR_ALREADY_EXISTS;
        }
    }

    if (mount_count >= VFS_MAX_MOUNTS) {
        return OS_ERR_TOO_MANY_MOUNTS;
    }

    vfs_mount_t *mount = &mounts[mount_count];

    memcpy(mount->path, path, length + 1);
    mount->driver = driver;
    mount->context = context;

    mount_count++;

    return OS_OK;
}

/*
 * Resolves an absolute path to its most specific registered mount.
 *
 * Matches only at directory boundaries, so a mount at /volumes/windows
 * does not match /volumes/windows-old.
 *
 * Returns the filesystem-relative path through relative_path.
 * The returned pointer refers to the caller's original path.
 */
const vfs_mount_t *vfs_resolve(const char *path, const char **relative_path)
{
    if (relative_path != NULL) {
        *relative_path = NULL;
    }

    if (path == NULL || path[0] != '/') {
        return NULL;
    }

    const vfs_mount_t *best = NULL;
    size_t best_length = 0;

    for (size_t i = 0; i < mount_count; i++) {
        const vfs_mount_t *mount = &mounts[i];
        size_t length = strlen(mount->path);

        if (strncmp(path, mount->path, length) != 0) {
            continue;
        }

        if (length > 1 && path[length] != '/' && path[length] != '\0') {
            continue;
        }

        if (best == NULL || length > best_length) {
            best = mount;
            best_length = length;
        }
    }

    if (best != NULL && relative_path != NULL) {
        if (best_length == 1) {
            *relative_path = path;
        } else if (path[best_length] == '\0') {
            *relative_path = "/";
        } else {
            *relative_path = path + best_length;
        }
    }

    return best;
}
