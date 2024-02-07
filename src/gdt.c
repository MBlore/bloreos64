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
    Everything to manage the GDT.
*/
#include <gdt.h>
#include <cpu.h>
#include <str.h>

struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gdp;

/* Set an entry in the GDT */
void set_gdt_entry(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);

    // Upper 4 bits and lower 4 bits in granularity dictate the size
    // and behaviour of the memory segment.
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;

    gdt[num].access = access;
}

/* Initialize the GDT with minimal config */
void init_gdt()
{
    gdp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdp.base = (uint64_t)&gdt;

    // Null segment.
    set_gdt_entry(0, 0, 0, 0, 0);

    // Code segment.
    // 0x9A = present, executable, readable.
    // 0x20 = 64-bit code segment granularity.
    set_gdt_entry(1, 0, 0xFFFFF, 0x9A, 0x20);

    // Data segment.
    // 0x92 = present, writable.
    // 0x00 = 4K granularity, 32-bit.
    set_gdt_entry(2, 0, 0xFFFFF, 0x92, 0x00);

    lgdt((uint64_t*)&gdp);
    kprintf("Loading GDT at: %X", &gdp);
}