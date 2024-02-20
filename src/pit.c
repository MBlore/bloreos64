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
/*
    This is the physical memory manager, dealing with physical available memory only.
*/
#include <stdint.h>
#include <pit.h>
#include <cpu.h>

/*
 * Disables the PIT timer.
 * Performs a cli/sti.
*/
void pit_disable_timer()
{
    disable_interrupts();
    outb(0x43, 0x30);
    outb(0x40, 0);
    outb(0x40, 0);
    enable_interrupts();
}