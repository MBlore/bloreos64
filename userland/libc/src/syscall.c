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
#include "syscall.h"

/*
 * BloreOS x86-64 syscall entry.
 *
 * RAX: syscall number / return value
 * RDI: argument 1
 * RSI: argument 2
 * RDX: argument 3
 *
 * Errors are returned as negative errno values.
 */
int64_t __syscall3(uint64_t number,
                   uint64_t arg1,
                   uint64_t arg2,
                   uint64_t arg3)
{
    uint64_t result;

    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number),
          "D"(arg1),
          "S"(arg2),
          "d"(arg3)
        : "memory", "cc"
    );

    return (int64_t)result;
}
