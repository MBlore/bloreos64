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
#ifndef _BLOREOS_ABI_SYSCALL_H
#define _BLOREOS_ABI_SYSCALL_H

/*
 * BloreOS syscall ABI v1.
 *
 * Entry: int $0x80
 *
 * RAX = syscall number / return value
 * RDI = argument 1
 * RSI = argument 2
 * RDX = argument 3
 * R10 = argument 4
 * R8  = argument 5
 * R9  = argument 6
 *
 * Errors are returned as negative errno values.
 * All general-purpose registers except RAX are preserved.
 */

#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_EXIT    4

#endif