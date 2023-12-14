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
#ifndef _BLOREOS_IDT_H
#define _BLOREOS_IDT_H

#include <stdint.h>
#include <stddef.h>

struct idt_entry_64
{
    uint16_t offset_1;          // Handler location bits 0..15
    uint16_t selector;          // Codesegment descriptor selection
    uint8_t ist;                // IST offset in bits 0..2
    uint8_t type_attr;          // Segment selector flags
    uint16_t offset_2;          // Handler location bits 16..31
    uint32_t offset_3;          // Handler location bits 32..63
    uint32_t reserved; 
} __attribute__((packed));



#endif