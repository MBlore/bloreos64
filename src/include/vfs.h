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
#ifndef _BLOREOS_VFS_H
#define _BLOREOS_VFS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <error.h>

#define VFS_MAX_MOUNTS 16
#define VFS_MAX_MOUNT_PATH 256
#define VFS_MAX_NAME 768

#define VFS_OPEN_READ   0x01
#define VFS_OPEN_WRITE  0x02
#define VFS_OPEN_CREATE 0x04

typedef struct vfs_driver vfs_driver_t;

typedef enum {
    VFS_SEEK_SET,
    VFS_SEEK_CUR,
    VFS_SEEK_END
} vfs_seek_mode_t;

typedef enum {
    VFS_TYPE_FILE,
    VFS_TYPE_DIRECTORY
} vfs_type_t;

typedef struct {
    const vfs_driver_t *driver;
    void *context;
    void *private_data;
    uint32_t flags;
} vfs_file_t;

/*
 * Represents an open directory in the VFS.
 */
typedef struct {
    const vfs_driver_t *driver;
    void *context;
    void *private_data;
} vfs_dir_t;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    bool valid;
    bool has_time;
} vfs_datetime_t;

typedef struct {
    vfs_type_t type;
    uint64_t size;
    uint32_t attributes;
    vfs_datetime_t created;
    vfs_datetime_t modified;
    vfs_datetime_t accessed;
} vfs_stat_t;

typedef struct {
    char name[VFS_MAX_NAME];
    uint64_t size;
    bool is_directory;
    vfs_datetime_t created;
    vfs_datetime_t modified;
    vfs_datetime_t accessed;
} vfs_dirent_t;

struct vfs_driver {
    const char *name;

    os_error_t  (*mkdir)(void *context, const char *path);
    os_error_t  (*rmdir)(void *context, const char *path);
    os_error_t  (*opendir)(void *context, const char *path,
                        void **handle);
    os_error_t  (*readdir)(void *handle, vfs_dirent_t *entry,
                        bool *has_entry);
    void        (*closedir)(void *handle);
    os_error_t  (*stat)(void *context, const char *path, vfs_stat_t *stat);
    os_error_t  (*open)(void *context, const char *path, uint32_t flags, void **handle);
    int64_t     (*read)(void *handle, void *buffer, size_t count);
    int64_t     (*write)(void *handle, const void *buffer, size_t count);
    int64_t     (*seek)(void *handle, int64_t offset, vfs_seek_mode_t mode);
    void        (*close)(void *handle);
    os_error_t  (*unlink)(void *context, const char *path);
};

typedef struct vfs_mount {
    char path[VFS_MAX_MOUNT_PATH];
    const vfs_driver_t *driver;
    void *context;
} vfs_mount_t;

void                vfs_init(void);
os_error_t          vfs_mount(const char *path, const vfs_driver_t *driver, void *context);
const vfs_mount_t   *vfs_resolve(const char *path, const char **relative_path);
os_error_t          vfs_mkdir(const char *path);
os_error_t          vfs_rmdir(const char *path);
os_error_t          vfs_opendir(const char *path, vfs_dir_t *dir);

os_error_t          vfs_readdir(vfs_dir_t *dir, vfs_dirent_t *entry,
                       bool *has_entry);

void                vfs_closedir(vfs_dir_t *dir);
os_error_t          vfs_stat(const char *path, vfs_stat_t *stat);
os_error_t          vfs_open(const char *path, uint32_t flags, vfs_file_t *file);
int64_t             vfs_read(vfs_file_t *file, void *buffer, size_t count);
int64_t             vfs_write(vfs_file_t *file, const void *buffer, size_t count);
int64_t             vfs_seek(vfs_file_t *file, int64_t offset, vfs_seek_mode_t mode);
void                vfs_close(vfs_file_t *file);
os_error_t          vfs_unlink(const char *path);

#endif