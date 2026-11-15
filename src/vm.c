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
    This is the virtual memory manager, dealing with all things virtual and paging.
*/
#include <stdbool.h>
#include <vm.h>
#include <str.h>
#include <cpu.h>
#include <math.h>
#include <mem.h>

#define PAGE_PRESENT    0x1
#define PAGE_RW         0x2
#define PAGE_USER       0x4
#define PAGE_PS (1ULL << 7)

#define PAGE_MAP_ALLOWED_FLAGS (PAGE_RW | PAGE_USER)

#define PAGE_SIZE_2MB (2ULL * 1024 * 1024)
#define PAGE_SIZE_1GB (1ULL * 1024 * 1024 * 1024)

// Macros for extracting page entry indexes from a virtual address (table 4.2 in intel SDM vol-3).
// Note: Remember, virtual addresses are just encoded page entries, containing the 4 keys in the virtual map lookup.
#define PML5_INDEX(va) (((va) >> 48) & 0x1FF)
#define PML4_INDEX(va) (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va) (((va) >> 30) & 0x1FF)
#define PD_INDEX(va) (((va) >> 21) & 0x1FF)
#define PT_INDEX(va) (((va) >> 12) & 0x1FF)
#define PAGE_OFFSET(va) ((va) & 0xFFF)

#define VIRTUAL_ADDRESS_BITS_4LEVEL 48
#define VIRTUAL_ADDRESS_BITS_5LEVEL 57

// Converts a physical address to the virtual direct memory map address.
#define PHYS_TO_VIRT(addr) ((void*)((uint64_t)(addr) + vmm_higher_half_offset))

uint32_t maxphyaddr;
uint32_t maxlinaddr;

static bool is_canonical_address(uint64_t virt_addr)
{
    // Determine the virtual address width from the active paging mode.
    const uint32_t address_bits = (get_cr4() & CR4_LA57)
        ? VIRTUAL_ADDRESS_BITS_5LEVEL
        : VIRTUAL_ADDRESS_BITS_4LEVEL;

    const uint64_t sign_bit =
        1ULL << (address_bits - 1);

    const uint64_t upper_mask =
        ~((1ULL << address_bits) - 1);

    // The upper bits must be copies of the sign bit.
    const uint64_t expected_upper =
        (virt_addr & sign_bit) ? upper_mask : 0;

    return (virt_addr & upper_mask) == expected_upper;
}

bool vm_translate(uint64_t virt_addr, uint64_t *phys_addr)
{
    // Reject an invalid output pointer.
    if (phys_addr == NULL) {
        return false;
    }

    // Reject non-canonical virtual addresses before walking.
    if (!is_canonical_address(virt_addr)) {
        return false;
    }

    // virt_addr layout:
    //  47:39     38:30   29:21  20:12  11:0
    // |  PML4  |  PDPT  |  PD  |  PT  | Offset |

    // Let's walk from the CR3 address which at the moment, is the Kernels virtual memory map.
    // Meaning, the CR3 is the START of the entire virtual memory layout starting at the 4th level of paging.
    // Note how the keys in to the tables are being extracted from the 'virt_addr'.

    uint64_t cr3 = get_cr3();

    // According to the Intel SDM, MAXPHYADDR specifies the number
    // of physical address bits supported by the processor.
    //
    // For MAXPHYADDR = 40, physical addresses occupy bits 0-39.
    //
    // Page-table physical addresses are 4 KiB aligned, so their
    // lowest 12 bits are zero.
    //
    // Therefore, the physical base address stored in a page-table
    // entry occupies bits 12 through MAXPHYADDR-1 (bits 12-39).
    //
    // Mask out the lower 12 flag bits and all bits above MAXPHYADDR-1.
    uint64_t addrmask = (((uint64_t)1 << maxphyaddr) - 1) & ~0xFFFULL;
    uint64_t pml4addr = cr3 & addrmask;

    // In five-level paging, CR3 points to the PML5.
    if (get_cr4() & CR4_LA57) {
        uint64_t *pml5 = (uint64_t *)PHYS_TO_VIRT(pml4addr);
        uint64_t pml5e = pml5[PML5_INDEX(virt_addr)];

        if (!(pml5e & PAGE_PRESENT)) {
            return false;
        }

        // Extract the physical address of the PML4.
        pml4addr = pml5e & addrmask;
    }

    uint64_t *pml4 = (uint64_t*)PHYS_TO_VIRT(pml4addr);

    // Get the PML4 entry for this virtual address. Since CR3 is the table for the 4th level, we index in to it using the
    // index contained in the virtual address.
    uint64_t pml4e = pml4[PML4_INDEX(virt_addr)];
    if (!(pml4e & PAGE_PRESENT)) {
        // Page not present halts the walking.
        return false;
    }

    // Now we can find the physical location of the next 3rd level (PDPT) table.
    // Note: Applying the mask gets us the physical address as some bits are reserved.
    uint64_t *pdpt = (uint64_t*)PHYS_TO_VIRT(pml4e & addrmask);

    // Now get the entry in the 3rd table.
    uint64_t pdpte = pdpt[PDPT_INDEX(virt_addr)];
    if (!(pdpte & PAGE_PRESENT)) {
        return false;
    }

    // A 1 GiB page terminates the walk at the PDPT.
    if (pdpte & PAGE_PS) {
        // With a 1 GiB page, it's the lowest 30 bits for offset.
        const uint64_t offset_mask = PAGE_SIZE_1GB - 1;
        const uint64_t base_mask = addrmask & ~offset_mask;

        *phys_addr = (pdpte & base_mask) | (virt_addr & offset_mask);
        return true;
    }

    // Now we can find the physical location of the next 2nd level (PD) table.
    uint64_t *pd = (uint64_t*)PHYS_TO_VIRT(pdpte & addrmask);

    // Now get the entry in the 2nd table.
    uint64_t pde = pd[PD_INDEX(virt_addr)];
    if (!(pde & PAGE_PRESENT)) {
        return false;
    }

    // A large page bit check terminates the walk at the Page Directory.
    if (pde & PAGE_PS) {
        // With a 2 MiB page, it's the lowest 21 bits for offset.
        const uint64_t offset_mask = PAGE_SIZE_2MB - 1;
        const uint64_t base_mask = addrmask & ~offset_mask;

        *phys_addr = (pde & base_mask) | (virt_addr & offset_mask);
        return true;
    }

    // On to the 1st level (PT) table.
    uint64_t *pt = (uint64_t*)PHYS_TO_VIRT(pde & addrmask);

    // Now get the entry in the final table.
    uint64_t pte = pt[PT_INDEX(virt_addr)];
        
    if (!(pte & PAGE_PRESENT)) {
        return false;
    }

    // Now we can get the actual physical address from the final PT entry.
    // We get the offset bits from the virt_addr (12 bits), and put them in the lower
    // 12 bits of the phys_addr that the mask zero'd out.
    *phys_addr = (pte & addrmask) | PAGE_OFFSET(virt_addr);
    return true;
}

// Maximum intermediate tables required for five-level paging.
#define MAX_NEW_PAGE_TABLES 4

// Records a newly allocated page table for rollback.
typedef struct {
    uint64_t *entry;
    uint64_t original_value;
    void *table;
} vm_allocation_t;

static bool allocate_page_table(uint64_t *entry, vm_allocation_t *allocation, uint64_t flags)
{
    // Allocate one physical page for the new page table.
    void *table = kpalloc(1);

    if (table == NULL) {
        return false;
    }

    // Initialise all page table entries to zero.
    memset(table, 0, PAGE_SIZE);

    // Convert the higher-half virtual address to a physical address.
    const uint64_t physical_address =
        (uint64_t)table - vmm_higher_half_offset;

    // Record the allocation before modifying the parent entry.
    allocation->entry = entry;
    allocation->original_value = *entry;
    allocation->table = table;

    // Intermediate entries are writable and optionally user-accessible.
    const uint64_t parent_flags =
        PAGE_PRESENT | PAGE_RW | (flags & PAGE_USER);

    // Link the new page table into its parent.
    *entry = physical_address | parent_flags;

    return true;
}

static void rollback_page_tables(vm_allocation_t *allocations,
                                 size_t allocation_count,
                                 uint64_t virt_addr)
{
    // Nothing to roll back if no tables were allocated.
    if (allocation_count == 0) {
        return;
    }

    // Restore the original paging hierarchy in reverse order.
    for (size_t i = allocation_count; i > 0; i--) {
        vm_allocation_t *allocation = &allocations[i - 1];
        *allocation->entry = allocation->original_value;
    }

    // Invalidate cached paging information before releasing memory.
    invlpg(virt_addr);

    // Release the allocated physical pages in reverse order.
    for (size_t i = allocation_count; i > 0; i--) {
        kfree(allocations[i - 1].table);
    }
}

static bool page_permissions_compatible(uint64_t entry,
                                        uint64_t flags)
{
    // A writable mapping requires a writable parent entry.
    if ((flags & PAGE_RW) && !(entry & PAGE_RW)) {
        return false;
    }

    // A user-accessible mapping requires a user-accessible parent.
    if ((flags & PAGE_USER) && !(entry & PAGE_USER)) {
        return false;
    }

    return true;
}

bool vm_map_page(uint64_t virt_addr,
                 uint64_t phys_addr,
                 uint64_t flags)
{
    // Reject unsupported flags before modifying any paging structures.
    if (flags & ~(uint64_t)PAGE_MAP_ALLOWED_FLAGS) {
        return false;
    }

    // Both addresses must be aligned to a 4 KiB page boundary.
    const uint64_t alignment_mask = PAGE_SIZE - 1;

    if (virt_addr & alignment_mask) {
        return false;
    }

    if (phys_addr & alignment_mask) {
        return false;
    }

    // Reject non-canonical virtual addresses.
    if (!is_canonical_address(virt_addr)) {
        return false;
    }

    // Reject physical addresses exceeding the CPU's address width.
    const uint64_t physical_address_mask =
        (1ULL << maxphyaddr) - 1;

    if (phys_addr & ~physical_address_mask) {
        return false;
    }

    // Extract physical addresses from page table entries.
    const uint64_t addrmask =
        physical_address_mask & ~alignment_mask;

    // Track page tables allocated during this mapping operation.
    vm_allocation_t allocations[MAX_NEW_PAGE_TABLES];
    size_t allocation_count = 0;

    // Obtain the physical address of the root paging structure.
    uint64_t pml4addr = get_cr3() & addrmask;

    // In five-level paging, CR3 points to the PML5.
    if (get_cr4() & CR4_LA57) {
        uint64_t *pml5 = PHYS_TO_VIRT(pml4addr);
        uint64_t *pml5e = &pml5[PML5_INDEX(virt_addr)];

        // Allocate a PML4 if one does not exist.
        if (!(*pml5e & PAGE_PRESENT)) {
            if (!allocate_page_table(
                    pml5e, &allocations[allocation_count], flags)) {
                rollback_page_tables(allocations, allocation_count, virt_addr);
                return false;
            }

            allocation_count++;
        }

        // Verify inherited permissions at the fifth level.
        if (!page_permissions_compatible(*pml5e, flags)) {
            rollback_page_tables(allocations, allocation_count, virt_addr);
            return false;
        }

        // Extract the physical address of the PML4.
        pml4addr = *pml5e & addrmask;
    }

    // Continue walking the standard four-level hierarchy.
    uint64_t *pml4 = PHYS_TO_VIRT(pml4addr);
    uint64_t *pml4e = &pml4[PML4_INDEX(virt_addr)];

    // Allocate a PDPT if one does not exist.
    if (!(*pml4e & PAGE_PRESENT)) {
        if (!allocate_page_table(pml4e, &allocations[allocation_count], flags)) {
            rollback_page_tables(allocations, allocation_count, virt_addr);
            return false;
        }

        allocation_count++;
    }

    if (!page_permissions_compatible(*pml4e, flags)) {
        rollback_page_tables(allocations, allocation_count, virt_addr);
        return false;
    }

    // Locate the Page Directory Pointer Table.
    uint64_t *pdpt = PHYS_TO_VIRT(*pml4e & addrmask);
    uint64_t *pdpte = &pdpt[PDPT_INDEX(virt_addr)];

    // Allocate a Page Directory if one does not exist.
    if (!(*pdpte & PAGE_PRESENT)) {
        if (!allocate_page_table(pdpte, &allocations[allocation_count], flags)) {
            rollback_page_tables(allocations, allocation_count, virt_addr);
            return false;
        }

        allocation_count++;
    }

    // Refuse to overwrite an existing 1 GiB mapping.
    if (*pdpte & PAGE_PS) {
        return false;
    }

    if (!page_permissions_compatible(*pdpte, flags)) {
        rollback_page_tables(allocations, allocation_count, virt_addr);
        return false;
    }

    // Locate the Page Directory.
    uint64_t *pd = PHYS_TO_VIRT(*pdpte & addrmask);
    uint64_t *pde = &pd[PD_INDEX(virt_addr)];

    // Allocate a page table if one does not exist.
    if (!(*pde & PAGE_PRESENT)) {
        if (!allocate_page_table(pde, &allocations[allocation_count], flags)) {
            rollback_page_tables(allocations, allocation_count, virt_addr);
            return false;
        }

        allocation_count++;
    }

    // Refuse to overwrite an existing 2 MiB mapping.
    if (*pde & PAGE_PS) {
        return false;
    }

    if (!page_permissions_compatible(*pde, flags)) {
        rollback_page_tables(allocations, allocation_count, virt_addr);
        return false;
    }

    // Locate the final Page Table entry.
    uint64_t *pt = PHYS_TO_VIRT(*pde & addrmask);
    uint64_t *pte = &pt[PT_INDEX(virt_addr)];

    // Never overwrite an existing mapping.
    if (*pte & PAGE_PRESENT) {
        return false;
    }

    // Create the mapping.
    *pte = phys_addr | flags | PAGE_PRESENT;

    // Invalidate cached translations for this virtual address.
    invlpg(virt_addr);

    return true;
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

    // Report address widths.
    uint64_t val = cpu_get_address_widths();
    maxphyaddr = val & 0xFF;
    maxlinaddr = val >> 8 & 0xFF;
    kprintf("MAXPHYADDR: %d bits\n", maxphyaddr);
    kprintf("MAXLINADDR: %d bits\n", maxlinaddr);
}