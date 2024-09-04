/*
    BloreOS - Operating System
    Copyright (C) 2023 Martin Blore

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
/*
    This is the virtual memory manager, dealing with all things virtual and paging for the kernel and user processes.
*/
#include <vm.h>
#include <str.h>
#include <cpu.h>
#include <math.h>
#include <mem.h>

// Macros for extracting page entry indexes from a virtual address (table 4.2 in intel SDM vol-3).
#define PML4_INDEX(va) (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va) (((va) >> 30) & 0x1FF)
#define PD_INDEX(va) (((va) >> 21) & 0x1FF)
#define PT_INDEX(va) (((va) >> 12) & 0x1FF)

// Converts a physical address to the virtual direct memory map address.
#define PHYS_TO_VIRT(addr) ((void*)((uint64_t)(addr) + vmm_higher_half_offset))

uint32_t maxphyaddr;
uint32_t maxlinaddr;

uint64_t *get_pdpt(uint64_t *pml4, uint64_t virt_addr)
{
    uint64_t pml4e = pml4[PML4_INDEX(virt_addr)];
    return (uint64_t*)PHYS_TO_VIRT(pml4e & ~0xFFF);
}

void vm_init()
{
    kprintf("Initializing virtual memory...\n");
    
    if (is_paging_enabled()) {
        kprintf("Paging enabled.\n");
    } else {
        kprintf("Paging disabled.\n");
    }

    // Check what paging mode we're in (assuming 64-bit mode so no need to check IA32_EFER.LME).
    uint64_t cr4 = get_cr4();

    if (!(cr4 & CR4_PAE)) {
        kprintf("32-bit paging mode.\n");
    }

    if (cr4 & CR4_PAE && !(cr4 & CR4_LA57)) {
        kprintf("4-level paging mode.\n");
    }

    if (cr4 & CR4_PAE && cr4 & CR4_LA57) {
        kprintf("5-level paging mode.\n");
    }

    /*
        Page Directory Pointer Table Entry (PDPTE)
        [0] - Present - Must be 1 to reference a page directory.
        [2:1] - Reserved (must be 0).
        [3] - Page-level write-through.
        [4] - Page-level cache disable
        [8:5] - Reserved (must be 0)
        [11:9] - Ignored
        [M-1:12] - Physical address of a 4KB aligned page directory referenced by this entry.
        [63:M] - Reserved (must be 0)

        M is an abbreviation for MAXPHYADDR, ,which is at most 52.
    */
    /*
        • CPUID.80000008H:EAX[7:0] reports the physical-address width supported by the processor. (For processors
            that do not support CPUID function 80000008H, the width is generally 36 if CPUID.01H:EDX.PAE [bit 6] = 1
            and 32 otherwise.) This width is referred to as MAXPHYADDR. MAXPHYADDR is at most 52.
        CPUID.80000008H:EAX[15:8] reports the linear-address width supported by the processor. Generally, this
        value is reported as follows:
        — If CPUID.80000001H:EDX.LM [bit 29] = 0, the value is reported as 32.
        — If CPUID.80000001H:EDX.LM [bit 29] = 1 and CPUID.(EAX=07H,ECX=0):ECX.LA57 [bit 16] = 0, the
        value is reported as 48.
        — If CPUID.(EAX=07H,ECX=0):ECX.LA57 [bit 16] = 1, the value is reported as 57.
        (Processors that do not support CPUID function 80000008H, support a linear-address width of 32.)
    */

    // Report address widths.
    uint64_t val = get_address_widths();
    maxphyaddr = val & 0xFF;
    maxlinaddr = val >> 8 & 0xFF;
    kprintf("MAXPHYADDR: %d bits\n", maxphyaddr);
    kprintf("MAXLINADDR: %d bits\n", maxlinaddr);

    // Let's walk from the CR3 address which at the moment, is the Kernels virtual memory map.
    uint64_t pml4_base = get_cr3();
    uint64_t *pml4 = (uint64_t*)PHYS_TO_VIRT(pml4_base);
}