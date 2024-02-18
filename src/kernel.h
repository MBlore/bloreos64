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

#ifndef _BLOREOS_KERNEL_H
#define _BLOREOS_KERNEL_H

#include <queue.h>

void report_cpu_details();

// Halt and catch fire function.
static inline void hcf(void)
{
    asm("cli");
    for (;;) {
        asm("hlt");
    }
}

extern CQueue_t *q_keyboard;

#endif