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
#ifndef _BLOREOS_DIRENT_H
#define _BLOREOS_DIRENT_H

#include <stdint.h>
#include <abi/dirent.h>

#define DIRENT_BUFFER_SIZE 1024
#define DIRENT_MAX_STREAMS 16

struct dirent {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[768];
};

typedef struct {
    int fd;
    uint32_t buffer_pos;
    uint32_t buffer_size;
    uint8_t buffer[DIRENT_BUFFER_SIZE];
    struct dirent entry;
} DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#endif
