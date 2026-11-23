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
#include <sys/stat.h>
#include <abi/fcntl.h>
#include <abi/syscall.h>
#include <stdint.h>
#include <stddef.h>
#include "syscall.h"

/*
 * Retrieves file metadata through the kernel's fstatat syscall.
 *
 * Currently only supports flags = 0 and paths relative to AT_FDCWD
 * or absolute paths. Returns -1 on error.
 */
int fstatat(int dirfd, const char *path, struct stat *stat, int flags)
{
    if (path == NULL || stat == NULL || flags != 0) {
        return -1;
    }

    int64_t result = __syscall3(
        SYS_FSTATAT,
        (uint64_t)(int64_t)dirfd,
        (uint64_t)path,
        (uint64_t)stat
    );

    return result < 0 ? -1 : 0;
}

/*
 * Retrieves metadata for a file or directory using its pathname.
 */
int stat(const char *path, struct stat *stat)
{
    return fstatat(AT_FDCWD, path, stat, 0);
}