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
#include <stdint.h>
#include <str.h>
#include <usermode.h>

/*
 * Dispatches a system call requested through interrupt 0x80.
 *
 * The initial implementation recognises system call 1 as a diagnostic
 * operation. Unknown calls return -1.
 */
uint64_t syscall_dispatch(uint64_t number, uint64_t caller_cpl)
{
    if (number == 1) {
        klogf("SYSCALL: Caller CPL = %lu\n", caller_cpl);
        klogf("Ring 3 program: Hello from user space!\n");
        return 0;
    }

    if (number == 2) {
        klogf("Ring 3 program: Exiting\n");
        usermode_exit();
    }

    return (uint64_t)-1;
}