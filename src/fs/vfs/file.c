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
 * Opens a file through the filesystem mounted at the supplied path.
 *
 * The driver allocates its private handle. The VFS stores the driver
 * and mount context so subsequent operations require no path lookup.
 */
os_error_t vfs_open(const char *path, uint32_t flags, vfs_file_t *file)
{
    if (path == NULL || path[0] != '/' || file == NULL) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    // Validate flag combinations.
    if (!(flags & (VFS_OPEN_READ | VFS_OPEN_WRITE)) ||
        (flags & ~(VFS_OPEN_READ | VFS_OPEN_WRITE | VFS_OPEN_CREATE)) ||
        ((flags & VFS_OPEN_CREATE) && !(flags & VFS_OPEN_WRITE))) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    const char *relative_path = NULL;
    const vfs_mount_t *mount = vfs_resolve(path, &relative_path);

    if (mount == NULL) {
        return OS_ERR_NOT_MOUNTED;
    }

    if (mount->driver->open == NULL) {
        return OS_ERR_NOT_SUPPORTED;
    }

    void *handle = NULL;
    os_error_t result = mount->driver->open(mount->context, relative_path, flags, &handle);

    if (result != OS_OK) {
        return result;
    }

    if (handle == NULL) {
        return OS_ERR_IO;
    }

    file->driver = mount->driver;
    file->context = mount->context;
    file->private_data = handle;
    file->flags = flags;

    return OS_OK;
}

/*
 * Reads data from an open file through its filesystem driver.
 *
 * Returns the number of bytes read, zero at EOF, or -1 on error.
 */
int64_t vfs_read(vfs_file_t *file, void *buffer, size_t count)
{
    if (file == NULL || file->private_data == NULL || file->driver == NULL ||
        !(file->flags & VFS_OPEN_READ) || (count > 0 && buffer == NULL)) {
        return -1;
    }

    if (file->driver->read == NULL) {
        return -1;
    }

    return file->driver->read(file->private_data, buffer, count);
}

/*
 * Writes data to an open file through its filesystem driver.
 *
 * Returns the number of bytes written, or -1 on error.
 */
int64_t vfs_write(vfs_file_t *file, const void *buffer, size_t count)
{
    if (file == NULL || file->private_data == NULL || file->driver == NULL ||
        !(file->flags & VFS_OPEN_WRITE) || (count > 0 && buffer == NULL)) {
        return -1;
    }

    if (file->driver->write == NULL) {
        return -1;
    }

    return file->driver->write(file->private_data, buffer, count);
}

/*
 * Changes the position of an open file.
 *
 * Returns the new absolute offset, or -1 on error.
 */
int64_t vfs_seek(vfs_file_t *file, int64_t offset, vfs_seek_mode_t mode)
{
    if (file == NULL || file->private_data == NULL || file->driver == NULL) {
        return -1;
    }

    if (mode != VFS_SEEK_SET && mode != VFS_SEEK_CUR && mode != VFS_SEEK_END) {
        return -1;
    }

    if (file->driver->seek == NULL) {
        return -1;
    }

    return file->driver->seek(file->private_data, offset, mode);
}

/*
 * Closes an open file and releases its driver-owned resources.
 *
 * Resets the public handle so it cannot accidentally be reused.
 */
void vfs_close(vfs_file_t *file)
{
    if (file == NULL) {
        return;
    }

    if (file->private_data != NULL && file->driver != NULL && file->driver->close != NULL) {
        file->driver->close(file->private_data);
    }

    memset(file, 0, sizeof(vfs_file_t));
}
