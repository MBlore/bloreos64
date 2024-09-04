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
#ifndef _BLOREOS_GDT_H
#define _BLOREOS_GDT_H

#include <stdint.h>
#include <stddef.h>

#define GDT_ENTRIES             3

#define SEGMENT_PRESENT         0x80 // Bit 7
#define SEGMENT_EXECUTABLE      0x08 // Bit 3
#define SEGMENT_CONFORM         0x04 // Bit 2
#define SEGMENT_READABLE        0x02 // Bit 1
#define SEGMENT_WRITABLE        0x02 // Bit 1
#define SEGMENT_ACCESSED        0x01 // Bit 0

#define SEGMENT_GRANULARITY_4KB     0x80 // Bit 7: 10000000 (Granularity 4KB)
#define SEGMENT_GRANULARITY_BYTE    0x00 // Bit 7: 00000000 (Granularity byte)
#define SEGMENT_SIZE_16BIT          0x00 // Bit 6: 00000000 (16-bit segment)
#define SEGMENT_SIZE_32BIT          0x40 // Bit 6: 01000000 (32-bit segment)
#define SEGMENT_LONG_MODE           0x20 // Bit 5: 00100000 (64-bit segment)

#define DESCRIPTOR_PRIVILEGE0       0x00 // Bits 6-5: 00 (Ring 0)
#define DESCRIPTOR_PRIVILEGE1       0x20 // Bits 6-5: 01 (Ring 1)
#define DESCRIPTOR_PRIVILEGE2       0x40 // Bits 6-5: 10 (Ring 2)
#define DESCRIPTOR_PRIVILEGE3       0x60 // Bits 6-5: 11 (Ring 3)
#define DESCRIPTOR_TYPE_CODE        0x10 // Bit 4: 00010000 (1 for code/data segment)
#define DESCRIPTOR_TYPE_DATA        0x10 // Bit 4: 00010000 (Data segment)




struct gdt_entry
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void init_gdt();

#endif