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
#ifndef _BLOREOS_ERROR_H
#define _BLOREOS_ERROR_H

typedef enum {
    OS_OK = 0,

    // General errors
    OS_ERR_INVALID_ARGUMENT,
    OS_ERR_NOT_FOUND,
    OS_ERR_ALREADY_EXISTS,
    OS_ERR_ACCESS_DENIED,
    OS_ERR_NOT_SUPPORTED,
    OS_ERR_OUT_OF_MEMORY,
    OS_ERR_BUSY,

    // Filesystem and storage errors
    OS_ERR_IO,
    OS_ERR_READ_ONLY,
    OS_ERR_NO_SPACE,
    OS_ERR_NOT_DIRECTORY,
    OS_ERR_IS_DIRECTORY,
    OS_ERR_DIRECTORY_NOT_EMPTY,
    OS_ERR_NAME_TOO_LONG,
    OS_ERR_TOO_MANY_MOUNTS,
    OS_ERR_NOT_MOUNTED,
    OS_ERR_INVALID_FILESYSTEM,
    OS_ERR_UNSUPPORTED_FILESYSTEM,
    OS_ERR_INVALID_GEOMETRY,
    OS_ERR_CORRUPT_FILESYSTEM
} os_error_t;

const char *os_error_string(os_error_t error);

#endif