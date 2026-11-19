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
#include <error.h>

/*
 * Returns a human-readable description of a BloreOS error code.
 *
 * The returned string is static and must not be modified or freed.
 * Unknown error codes return a generic description.
 */
const char *os_error_string(os_error_t error)
{
    switch (error) {
        case OS_OK:
            return "Success";

        case OS_ERR_INVALID_ARGUMENT:
            return "Invalid argument";

        case OS_ERR_NOT_FOUND:
            return "Not found";

        case OS_ERR_ALREADY_EXISTS:
            return "Already exists";

        case OS_ERR_ACCESS_DENIED:
            return "Access denied";

        case OS_ERR_NOT_SUPPORTED:
            return "Operation not supported";

        case OS_ERR_OUT_OF_MEMORY:
            return "Out of memory";

        case OS_ERR_BUSY:
            return "Resource busy";

        case OS_ERR_IO:
            return "I/O error";

        case OS_ERR_READ_ONLY:
            return "Filesystem is read-only";

        case OS_ERR_NO_SPACE:
            return "No space available";

        case OS_ERR_NOT_DIRECTORY:
            return "Not a directory";

        case OS_ERR_IS_DIRECTORY:
            return "Is a directory";

        case OS_ERR_DIRECTORY_NOT_EMPTY:
            return "Directory is not empty";

        case OS_ERR_NAME_TOO_LONG:
            return "Name too long";

        case OS_ERR_TOO_MANY_MOUNTS:
            return "Too many mounted filesystems";

        case OS_ERR_NOT_MOUNTED:
            return "Filesystem is not mounted";

        case OS_ERR_INVALID_FILESYSTEM:
            return "Invalid filesystem";

        case OS_ERR_UNSUPPORTED_FILESYSTEM:
            return "Unsupported filesystem";

        case OS_ERR_INVALID_GEOMETRY:
            return "Invalid filesystem geometry";

        case OS_ERR_CORRUPT_FILESYSTEM:
            return "Corrupt filesystem";

        default:
            return "Unknown error";
    }
}
