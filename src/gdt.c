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
struct gdt_ptr gdtp;

/* Set an entry in the GDT */
void set_gdt_entry(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    // The base and limit are not used in 64-bit long mode.
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

// Loads the GDT and reloads the segment registers which is required after altering the GDT.
void _gdt_reload()
{
    // GDT segment selectors are in multiples of 8. 0 = 1st entry, 8 = 2nd entry etc.

    asm volatile(
        "lgdt %0\n\t"               // Loads the GDT in to memory.
        "push $0x08\n\t"            // Code segment selector in the GDT, entry 1.
        "lea 1f(%%rip), %%rax\n\t"  // Loading relative address of the label.
        "push %%rax\n\t"
        "lretq\n\t"                 // Performing a long return to our label below.
        "1:\n\t"
        "mov $0x10, %%eax\n\t"      // Data segment selector in the GDT, entry 2.
        "mov %%eax, %%ds\n\t"
        "mov %%eax, %%es\n\t"
        "mov %%eax, %%fs\n\t"
        "mov %%eax, %%gs\n\t"
        "mov %%eax, %%ss\n\t"
        :
        : "m"(gdtp)
        : "rax", "memory"
    );
}

/* Initialize the GDT with minimal config */
void init_gdt()
{
    gdtp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdtp.base = (uint64_t)&gdt;

    // Null segment.
    set_gdt_entry(0, 0, 0, 0, 0);

    // Code segment.
    // 0x9A = present, executable, readable.
    // 0x20 = 64-bit code segment granularity.
    set_gdt_entry(1, 0, 0xFFFFFFFF, 0x9A, 0x20);

    // Data segment.
    // 0x92 = present, writable.
    // 0x00 = 4K granularity, 32-bit.
    set_gdt_entry(2, 0, 0xFFFFFFFF, 0x92, 0x00);

    _gdt_reload();

    kprintf("Loading GDT at: 0x%X\n", &gdtp);
}