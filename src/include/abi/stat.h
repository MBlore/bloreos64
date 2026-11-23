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
#ifndef _BLOREOS_ABI_STAT_H
#define _BLOREOS_ABI_STAT_H

#include <stdint.h>

#define S_IFMT  0170000
#define S_IFREG 0100000
#define S_IFDIR 0040000

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t valid;
    uint8_t has_time;
} os_stat_time_t;

struct stat {
    uint64_t st_size;
    uint32_t st_mode;
    uint32_t st_attributes;
    os_stat_time_t st_ctime;
    os_stat_time_t st_mtime;
    os_stat_time_t st_atime;
};

#endif