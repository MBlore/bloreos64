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
#include <unistd.h>
#include <abi/syscall.h>
#include <stdint.h>

#include "syscall.h"

/*
 * Writes bytes to a file descriptor through the kernel.
 *
 * Returns the number of bytes written, or -1 on failure.
 * Currently only stdout and stderr are supported by SYS_WRITE.
 */
ssize_t write(int fd, const void *buffer, size_t count)
{
    int64_t result = __syscall3(SYS_WRITE, (uint64_t)fd, (uint64_t)buffer, (uint64_t)count);

    return result < 0 ? -1 : (ssize_t)result;
}