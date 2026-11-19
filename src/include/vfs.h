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

typedef struct {
    char name[VFS_MAX_NAME];
    uint64_t size;
    bool is_directory;
} vfs_dirent_t;

typedef struct vfs_driver {
    const char *name;

    os_error_t (*mkdir)(void *context, const char *path);
    os_error_t (*rmdir)(void *context, const char *path);
    os_error_t (*listdir)(void *context, const char *path,
                      os_error_t (*callback)(const vfs_dirent_t *entry, void *context),
                      void *callback_context);
} vfs_driver_t;

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
os_error_t          vfs_listdir(const char *path,
                                os_error_t (*callback)(const vfs_dirent_t *entry, void *context),
                                void *context);

#endif