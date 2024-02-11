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
#include <ps2.h>
#include <cpu.h>

#define PORT_PS2_DATA (uint16_t)0x60
#define PORT_PS2_STATUSCMD (uint16_t)0x64

uint8_t ps2_read_key()
{
    uint8_t status = inb(PORT_PS2_STATUSCMD);

    if (status & 1) {
        // We have key data to read.
        return inb(PORT_PS2_DATA);
    }

    return 0;
}
