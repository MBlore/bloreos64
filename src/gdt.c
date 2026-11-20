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
#include <string.h>

#define TSS_STACK_SIZE 16384

struct gdt_entry gdt[GDT_ENTRIES];
struct gdt_ptr gdtp;

static struct tss64 tss;
static uint8_t tss_stack[TSS_STACK_SIZE] __attribute__((aligned(16)));

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

    // Load the Task Register with our TSS selector.
    uint16_t tss_selector = GDT_TSS_SELECTOR;
    asm volatile("ltr %0" : : "rm"(tss_selector) : "memory");
}

/*
 * Installs a 64-bit TSS descriptor into GDT entries 5 and 6.
 *
 * The TSS provides the Ring 0 stack used when the CPU handles
 * interrupts and exceptions originating from user mode.
 */
static void gdt_install_tss(void)
{
    uint64_t base = (uint64_t)&tss;
    uint32_t limit = sizeof(tss) - 1;

    // Configure the Ring 0 stack and disable the I/O permission bitmap.
    tss.rsp0 = (uint64_t)(tss_stack + sizeof(tss_stack));
    tss.iomap_base = sizeof(tss);

    // Clear both entries occupied by the 16-byte TSS descriptor.
    memset(&gdt[5], 0, sizeof(struct gdt_entry) * 2);

    gdt[5].limit_low = limit & 0xFFFF;
    gdt[5].base_low = base & 0xFFFF;
    gdt[5].base_middle = (base >> 16) & 0xFF;
    gdt[5].access = 0x89;
    gdt[5].granularity = (limit >> 16) & 0x0F;
    gdt[5].base_high = (base >> 24) & 0xFF;

    // Upper 32 bits of the 64-bit TSS base address.
    uint32_t base_upper = (uint32_t)(base >> 32);
    memcpy(&gdt[6], &base_upper, sizeof(base_upper));
}

/* Initialize the GDT with minimal config */
void init_gdt()
{
    gdtp.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdtp.base = (uint64_t)&gdt;

    // Null segment.
    set_gdt_entry(0, 0, 0, 0, 0);

    // Kernel Code segment (0x08).
    set_gdt_entry(1, 0, 0xFFFFFFFF, (SEGMENT_PRESENT | DESCRIPTOR_PRIVILEGE0 | DESCRIPTOR_TYPE_CODE | SEGMENT_EXECUTABLE | SEGMENT_READABLE), SEGMENT_LONG_MODE);

    // Kernal Data segment (0x10).
    set_gdt_entry(2, 0, 0xFFFFFFFF, (SEGMENT_PRESENT | DESCRIPTOR_PRIVILEGE0 | DESCRIPTOR_TYPE_DATA | SEGMENT_WRITABLE), (SEGMENT_GRANULARITY_BYTE | SEGMENT_SIZE_16BIT));

    // User Data segment (0x18, Ring 3 selector 0x1B).
    set_gdt_entry(3, 0, 0xFFFFFFFF,
                SEGMENT_PRESENT | DESCRIPTOR_PRIVILEGE3 | DESCRIPTOR_TYPE_DATA | SEGMENT_WRITABLE,
                SEGMENT_GRANULARITY_BYTE | SEGMENT_SIZE_16BIT);

    // User Code segment (0x20, Ring 3 selector 0x23).
    set_gdt_entry(4, 0, 0xFFFFFFFF,
                SEGMENT_PRESENT | DESCRIPTOR_PRIVILEGE3 | DESCRIPTOR_TYPE_CODE |
                SEGMENT_EXECUTABLE | SEGMENT_READABLE,
                SEGMENT_LONG_MODE);

    // Install the 64-bit Task State Segment.
    gdt_install_tss();

    _gdt_reload();

    kprintf("Loading GDT at: 0x%X\n", &gdtp);
}