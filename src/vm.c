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

#define PAGE_PRESENT    0x1
#define PAGE_RW         0x2
#define PAGE_USER       0x4

// Macros for extracting page entry indexes from a virtual address (table 4.2 in intel SDM vol-3).
// Note: Remember, virtual addresses are just encoded page entries, containing the 4 keys in the virtual map lookup.
#define PML4_INDEX(va) (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va) (((va) >> 30) & 0x1FF)
#define PD_INDEX(va) (((va) >> 21) & 0x1FF)
#define PT_INDEX(va) (((va) >> 12) & 0x1FF)
#define PAGE_OFFSET(va) ((va) & 0xFFF)

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
}

uint64_t walk_page_table(uint64_t virt_addr)
{
    // Let's walk from the CR3 address which at the moment, is the Kernels virtual memory map.
    // Meaning, the CR3 is the START of the entire virtual memory layout starting at the 4th level of paging.
    // Note how the keys in to the tables are being extracted from the 'virt_addr'.

    uint64_t cr3 = get_cr3();
    uint64_t *pml4 = (uint64_t*)PHYS_TO_VIRT(cr3);

    // Get the PML4 entry for this virtual address. Since CR3 is the table for the 4th level, we index in to it using the
    // index contained in the virtual address.
    uint64_t pml4e = pml4[PML4_INDEX(virt_addr)];
    if (!(pml4e & PAGE_PRESENT)) {
        // Page not present halts the walking.
        return 0;
    }

    // Now we can find the physical location of the next 3rd level (PDPT) table.
    // Note: Applying the mask gets us the physical address as some bits are reserved.
    uint64_t *pdpt = (uint64_t*)PHYS_TO_VIRT(pml4e & ~0xFFF); 

    // Now get the entry in the 3rd table.
    uint64_t pdpte = pdpt[PDPT_INDEX(virt_addr)];
    if (!(pdpte & PAGE_PRESENT)) {
        return 0;
    }

    // Now we can find the physical location of the next 2nd level (PD) table.
    uint64_t *pd = (uint64_t*)PHYS_TO_VIRT(pdpte & ~0xFFF);

    // Now get the entry in the 2nd table.
    uint64_t pde = pd[PD_INDEX(virt_addr)];
    if (!(pde & PAGE_PRESENT)) {
        return 0;
    }

    // On to the 1st level (PT) table.
    uint64_t *pt = (uint64_t*)PHYS_TO_VIRT(pde & ~0xFFF);

    // Now get the entry in the final table.
    uint64_t pte = pt[PT_INDEX(virt_addr)];
    if (!(pte & PAGE_PRESENT)) {
        return 0;
    }

    // Now we can get the actual physical address from the final PT entry.
    uint64_t phys_addr = (pte & ~0xFFF) | PAGE_OFFSET(virt_addr);
    return phys_addr;
}