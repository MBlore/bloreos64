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
#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <abi/unistd.h>

/*
 * Writes a complete buffer to standard output.
 *
 * Returns zero on success, otherwise -1.
 */
static int write_all(const char *buffer, size_t length)
{
    size_t written = 0;

    while (written < length) {
        ssize_t result = write(STDOUT_FILENO, buffer + written, length - written);

        if (result <= 0) {
            return -1;
        }

        written += (size_t)result;
    }

    return 0;
}

/*
 * Prints a NUL-terminated string followed by a newline.
 *
 * Returns a nonnegative value on success, otherwise EOF (-1).
 */
int puts(const char *string)
{
    size_t length = 0;

    while (string[length] != '\0') {
        length++;
    }

    if (write_all(string, length) != 0 || write_all("\n", 1) != 0) {
        return -1;
    }

    return 0;
}