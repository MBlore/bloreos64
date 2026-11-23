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
#ifndef _BLOREOS_ABI_DIRENT_H
#define _BLOREOS_ABI_DIRENT_H

#include <stdint.h>
#include <stddef.h>

/*
 * Linux-compatible getdents64 directory entry.
 *
 * d_name is variable-length and NUL-terminated.
 * Each record is aligned to 8 bytes.
 */
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

#define DIRENT64_ALIGN 8

#define DIRENT64_RECLEN(name_length) \
    ((offsetof(struct linux_dirent64, d_name) + \
      (name_length) + 1 + \
      (DIRENT64_ALIGN - 1)) & \
     ~(DIRENT64_ALIGN - 1))

#endif
