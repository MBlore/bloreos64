
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
#include <dirent.h>
#include <abi/fcntl.h>
#include <abi/syscall.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "syscall.h"

static DIR streams[DIRENT_MAX_STREAMS];
static bool stream_used[DIRENT_MAX_STREAMS];

DIR *opendir(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    int slot = -1;

    for (int i = 0; i < DIRENT_MAX_STREAMS; i++) {
        if (!stream_used[i]) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        return NULL;
    }

    int64_t fd = __syscall3(
        SYS_OPENAT,
        (uint64_t)(int64_t)AT_FDCWD,
        (uint64_t)path,
        O_RDONLY | O_DIRECTORY
    );

    if (fd < 0) {
        return NULL;
    }

    DIR *dir = &streams[slot];

    dir->fd = (int)fd;
    dir->buffer_pos = 0;
    dir->buffer_size = 0;

    stream_used[slot] = true;

    return dir;
}

struct dirent *readdir(DIR *dir)
{
    if (dir == NULL) {
        return NULL;
    }

    if (dir->buffer_pos >= dir->buffer_size) {
        int64_t count = __syscall3(
            SYS_GETDENTS64,
            (uint64_t)dir->fd,
            (uint64_t)dir->buffer,
            sizeof(dir->buffer)
        );

        if (count <= 0) {
            return NULL;
        }

        dir->buffer_pos = 0;
        dir->buffer_size = (uint32_t)count;
    }

    struct linux_dirent64 *entry =
        (struct linux_dirent64 *)
        (dir->buffer + dir->buffer_pos);

    size_t remaining = dir->buffer_size - dir->buffer_pos;

    if (remaining < offsetof(struct linux_dirent64, d_name) + 1 ||
        entry->d_reclen > remaining ||
        entry->d_reclen <
            offsetof(struct linux_dirent64, d_name) + 1 ||
        (entry->d_reclen & 7) != 0) {
        return NULL;
    }

    dir->buffer_pos += entry->d_reclen;

    dir->entry.d_ino = entry->d_ino;
    dir->entry.d_type = entry->d_type;

    size_t i = 0;
    size_t capacity = sizeof(dir->entry.d_name);

    while (i + 1 < capacity && entry->d_name[i] != '\0') {
        dir->entry.d_name[i] = entry->d_name[i];
        i++;
    }

    dir->entry.d_name[i] = '\0';

    return &dir->entry;
}

int closedir(DIR *dir)
{
    if (dir == NULL) {
        return -1;
    }

    for (int i = 0; i < DIRENT_MAX_STREAMS; i++) {
        if (&streams[i] == dir && stream_used[i]) {
            int64_t result = __syscall3(
                SYS_CLOSE,
                (uint64_t)dir->fd,
                0,
                0
            );

            if (result < 0) {
                return -1;
            }

            stream_used[i] = false;
            return 0;
        }
    }

    return -1;
}
