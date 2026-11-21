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

#define PAT_ENTRY_COUNT 8
#define PAT_ENTRY_BITS  8
#define PAT_ENTRY_MASK  0xFFULL

// Bits of the PAT selection index.
#define PAT_INDEX_PWT (1U << 0)
#define PAT_INDEX_PCD (1U << 1)
#define PAT_INDEX_PAT (1U << 2)

#define PAGE_PRESENT    0x1
#define PAGE_PS (1ULL << 7)

#define PAGE_MAP_ALLOWED_FLAGS (PAGE_RW | PAGE_USER | PAGE_PWT | PAGE_PCD | PAGE_PAT | PAGE_NX)

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

// Virtual address range reserved for MMIO mappings.
#define VM_MMIO_START 0xFFFFC00000000000ULL
#define VM_MMIO_END   0xFFFFC10000000000ULL

// Next available virtual address for MMIO mappings.
static uint64_t mmio_next_address = VM_MMIO_START;
static bool mmio_region_available = false;

uint32_t maxphyaddr;
uint32_t maxlinaddr;

static bool nx_supported = false;

/*
 * Returns whether NX execution protection is enabled on the current CPU.
 */
bool vm_nx_supported(void)
{
    return nx_supported;
}

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

/*
 * Detects and enables the NX page-table permission when supported.
 *
 * NX is optional for kernel operation. Unsupported CPUs continue
 * without execute-disable protection.
 */
static void vm_init_nx(void)
{
    uint32_t eax;
    uint32_t edx;

    cpuid(CPUID_EXTENDED_FEATURES, &eax, &edx);

    if (!(edx & CPUID_EDX_NX)) {
        kprintf("VMEM: Warning - NX is not supported.\n");
        return;
    }

    uint64_t efer = read_msr(IA32_EFER);

    if (!(efer & EFER_NXE)) {
        write_msr(IA32_EFER, efer | EFER_NXE);
    }

    nx_supported = (read_msr(IA32_EFER) & EFER_NXE) != 0;

    if (nx_supported) {
        kprintf("VMEM: NX enabled.\n");
    } else {
        kprintf("VMEM: Warning - NX could not be enabled.\n");
    }
}

/*
 * Creates a new process address space with its own top-level page table.
 *
 * The lower half starts unmapped, while the kernel's higher-half mappings
 * are inherited from the currently active address space.
 *
 * The returned page-table root is a physical address suitable for CR3.
 * The caller owns the newly allocated root page.
 */
bool vm_create_address_space(uint64_t *page_table_root)
{
    if (page_table_root == NULL) {
        return false;
    }

    if (maxphyaddr < 32 || maxphyaddr >= 64) {
        return false;
    }

    // Allocate a new top-level page table.
    uint64_t *new_root = kpalloc(1);

    if (new_root == NULL) {
        return false;
    }

    memset(new_root, 0, PAGE_SIZE);

    // Locate the currently active top-level page table.
    const uint64_t address_mask = ((1ULL << maxphyaddr) - 1) & ~(PAGE_SIZE - 1ULL);
    const uint64_t current_root = get_cr3() & address_mask;

    uint64_t *kernel_root = PHYS_TO_VIRT(current_root);

    // Both paging modes divide their top-level table into lower and upper halves.
    // Copy only the kernel half, preserving supervisor-only permissions.
    for (size_t i = 256; i < 512; i++) {
        new_root[i] = kernel_root[i];
    }

    *page_table_root = (uint64_t)new_root - vmm_higher_half_offset;

    return true;
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

/*
 * Maps a physical page into the specified address space.
 *
 * The page-table root is a physical address. Page-table structures
 * are accessed through the kernel's higher-half direct mapping.
 */
static bool vm_map_page_internal(uint64_t page_table_root, uint64_t virt_addr,
                                 uint64_t phys_addr, uint64_t flags)
{
    if ((flags & PAGE_NX) && !nx_supported) {
        return false;
    }

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

    // Validate root.
    if (page_table_root == 0 ||
        (page_table_root & (PAGE_SIZE - 1)) ||
        (page_table_root & ~physical_address_mask)) {
        return false;
    }

    // Track page tables allocated during this mapping operation.
    vm_allocation_t allocations[MAX_NEW_PAGE_TABLES];
    size_t allocation_count = 0;

    // Obtain the physical address of the root paging structure.
    uint64_t pml4addr = page_table_root & addrmask;

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
        rollback_page_tables(allocations, allocation_count, virt_addr);
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
        rollback_page_tables(allocations, allocation_count, virt_addr);
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
        rollback_page_tables(allocations, allocation_count, virt_addr);
        return false;
    }

    // Create the mapping.
    *pte = phys_addr | flags | PAGE_PRESENT;

    // Invalidate cached translations for this virtual address.
    invlpg(virt_addr);

    return true;
}

/*
 * Maps a physical page into the currently active address space.
 */
bool vm_map_page(uint64_t virt_addr, uint64_t phys_addr, uint64_t flags)
{
    const uint64_t address_mask = ((1ULL << maxphyaddr) - 1) & ~(PAGE_SIZE - 1ULL);
    const uint64_t current_root = get_cr3() & address_mask;

    return vm_map_page_internal(current_root, virt_addr, phys_addr, flags);
}

/*
 * Maps a user-accessible physical page into a specified address space.
 *
 * The target page tables are modified without switching CR3.
 */
bool vm_map_page_in(uint64_t page_table_root, uint64_t virt_addr,
                    uint64_t phys_addr, uint64_t flags)
{
    // Preserve our current policy of using the lower 48-bit user address range.
    if (virt_addr < PAGE_SIZE || virt_addr >= 0x0000800000000000ULL) {
        return false;
    }

    // Process mappings must be user-accessible.
    if (!(flags & PAGE_USER)) {
        return false;
    }

    return vm_map_page_internal(page_table_root, virt_addr, phys_addr, flags);
}

/*
 * Maps a physical page into the user portion of virtual memory.
 *
 * User mappings must be page-aligned and reside in the lower canonical
 * address range. Kernel mappings remain supervisor-only.
 */
bool vm_map_user_page(uint64_t virt_addr, uint64_t phys_addr, bool writable)
{
    // Restrict user mappings to the lower 48-bit canonical address range.
    if (virt_addr >= 0x0000800000000000ULL) {
        return false;
    }

    // Reject the null page.
    if (virt_addr < PAGE_SIZE) {
        return false;
    }

    uint64_t flags = PAGE_USER;

    if (writable) {
        flags |= PAGE_RW;
    }

    return vm_map_page(virt_addr, phys_addr, flags);
}

static bool vm_unmap_page_internal(uint64_t page_table_root, uint64_t virt_addr)
{
    if (maxphyaddr < 32 || maxphyaddr >= 64) {
        return false;
    }

    const uint64_t physical_address_mask = (1ULL << maxphyaddr) - 1;

    if (page_table_root == 0 ||
        (page_table_root & (PAGE_SIZE - 1)) ||
        (page_table_root & ~physical_address_mask)) {
        return false;
    }

    // The virtual address must be aligned to a 4 KiB page.
    const uint64_t alignment_mask = PAGE_SIZE - 1;

    if (virt_addr & alignment_mask) {
        return false;
    }

    // Reject non-canonical virtual addresses.
    if (!is_canonical_address(virt_addr)) {
        return false;
    }

    // Construct the physical address mask.
    const uint64_t addrmask = ((1ULL << maxphyaddr) - 1) & ~alignment_mask;

    // Obtain the physical address of the root paging structure.
    uint64_t pml4addr = page_table_root & addrmask;

    // Traverse the additional paging level if LA57 is enabled.
    if (get_cr4() & CR4_LA57) {
        uint64_t *pml5 = PHYS_TO_VIRT(pml4addr);
        uint64_t pml5e = pml5[PML5_INDEX(virt_addr)];

        if (!(pml5e & PAGE_PRESENT)) {
            return false;
        }

        // Extract the physical address of the PML4.
        pml4addr = pml5e & addrmask;
    }

    // Locate the PML4.
    uint64_t *pml4 = PHYS_TO_VIRT(pml4addr);
    uint64_t pml4e = pml4[PML4_INDEX(virt_addr)];

    if (!(pml4e & PAGE_PRESENT)) {
        return false;
    }

    // Traverse to the PDPT.
    uint64_t *pdpt = PHYS_TO_VIRT(pml4e & addrmask);
    uint64_t pdpte = pdpt[PDPT_INDEX(virt_addr)];

    // Reject missing entries and 1 GiB page mappings.
    if (!(pdpte & PAGE_PRESENT) || (pdpte & PAGE_PS)) {
        return false;
    }

    // Traverse to the page directory.
    uint64_t *pd = PHYS_TO_VIRT(pdpte & addrmask);
    uint64_t pde = pd[PD_INDEX(virt_addr)];

    // Reject missing entries and 2 MiB page mappings.
    if (!(pde & PAGE_PRESENT) || (pde & PAGE_PS)) {
        return false;
    }

    // Locate the target page table entry.
    uint64_t *pt = PHYS_TO_VIRT(pde & addrmask);
    uint64_t *pte = &pt[PT_INDEX(virt_addr)];

    if (!(*pte & PAGE_PRESENT)) {
        return false;
    }

    // Remove the virtual memory mapping.
    *pte = 0;

    // Invalidate the cached translation on the local CPU.
    invlpg(virt_addr);

    return true;
}

/*
 * Removes a 4 KiB mapping from the currently active address space.
 */
bool vm_unmap_page(uint64_t virt_addr)
{
    if (maxphyaddr < 32 || maxphyaddr >= 64) {
        return false;
    }

    const uint64_t address_mask = ((1ULL << maxphyaddr) - 1) & ~(PAGE_SIZE - 1ULL);
    const uint64_t current_root = get_cr3() & address_mask;

    return vm_unmap_page_internal(current_root, virt_addr);
}

/*
 * Removes a user-space mapping from a specified process address space.
 *
 * This does not release the mapped physical page.
 */
bool vm_unmap_page_in(uint64_t page_table_root, uint64_t virt_addr)
{
    // Preserve the lower 48-bit user address-space policy.
    if (virt_addr < PAGE_SIZE || virt_addr >= 0x0000800000000000ULL) {
        return false;
    }

    return vm_unmap_page_internal(page_table_root, virt_addr);
}

/*
 * Recursively releases private page-table structures.
 *
 * Level 1 contains leaf PTEs, which reference physical data pages.
 * Those data pages are not freed here.
 *
 * Large-page entries at levels 2 and 3 are also leaves.
 */
static void vm_free_page_table(uint64_t table_phys, unsigned int level, uint64_t address_mask)
{
    uint64_t *table = PHYS_TO_VIRT(table_phys);

    if (level > 1) {
        for (size_t i = 0; i < 512; i++) {
            const uint64_t entry = table[i];

            if (!(entry & PAGE_PRESENT)) {
                continue;
            }

            // A 2 MiB or 1 GiB mapping is a leaf, not another page table.
            if ((level == 2 || level == 3) && (entry & PAGE_PS)) {
                continue;
            }

            const uint64_t child_phys = entry & address_mask;
            vm_free_page_table(child_phys, level - 1, address_mask);
        }
    }

    kfree(table);
}

/*
 * Destroys an inactive process address space.
 *
 * Only the lower half of the top-level page table is private.
 * The upper half contains shared kernel mappings and must not be freed.
 *
 * The caller must release process-owned physical data pages separately.
 * The address space must not be active on any CPU.
 */
bool vm_destroy_address_space(uint64_t page_table_root)
{
    if (maxphyaddr < 32 || maxphyaddr >= 64) {
        return false;
    }

    const uint64_t address_mask = ((1ULL << maxphyaddr) - 1) & ~(PAGE_SIZE - 1ULL);

    if (page_table_root == 0 ||
        (page_table_root & (PAGE_SIZE - 1)) ||
        (page_table_root & ~address_mask)) {
        return false;
    }

    // Never destroy the address space currently used by this CPU.
    if ((get_cr3() & address_mask) == page_table_root) {
        return false;
    }

    const unsigned int root_level = (get_cr4() & CR4_LA57) ? 5 : 4;
    uint64_t *root = PHYS_TO_VIRT(page_table_root);

    // The upper half is shared with the kernel.
    for (size_t i = 0; i < 256; i++) {
        const uint64_t entry = root[i];

        if (!(entry & PAGE_PRESENT)) {
            continue;
        }

        const uint64_t child_phys = entry & address_mask;
        vm_free_page_table(child_phys, root_level - 1, address_mask);

        root[i] = 0;
    }

    // Release the process's top-level page table.
    kfree(root);

    return true;
}

bool vm_get_memory_type_flags(vm_memory_type_t type,
                              uint64_t *flags)
{
     if (flags == NULL)
        return false;

    // Verify that the processor supports PAT before reading its MSR.
    uint32_t eax, edx;
    cpuid(CPUID_FEATURES, &eax, &edx);

    if (!(edx & CPUID_EDX_PAT))
        return false;

    const uint64_t pat = read_msr(IA32_PAT);

    for (uint32_t i = 0; i < PAT_ENTRY_COUNT; i++) {
        // Extract the memory type from the current PAT entry.
        uint64_t entry = (pat >> (i * PAT_ENTRY_BITS)) & PAT_ENTRY_MASK;

        if (entry != (uint64_t)type)
            continue;

        // Convert the PAT index into page table entry flags.
        *flags = 0;

        if (i & PAT_INDEX_PWT)
            *flags |= PAGE_PWT;

        if (i & PAT_INDEX_PCD)
            *flags |= PAGE_PCD;

        if (i & PAT_INDEX_PAT)
            *flags |= PAGE_PAT;

        return true;
    }

    return false;
}

static bool is_mmio_region_available(void)
{
    const uint64_t address_mask =
        ((1ULL << maxphyaddr) - 1) & ~(PAGE_SIZE - 1ULL);

    uint64_t pml4addr = get_cr3() & address_mask;

    // Locate the PML4 when using five-level paging.
    if (get_cr4() & CR4_LA57)
    {
        uint64_t *pml5 = PHYS_TO_VIRT(pml4addr);
        uint64_t pml5e = pml5[PML5_INDEX(VM_MMIO_START)];

        // An absent parent means the entire region is available.
        if (!(pml5e & PAGE_PRESENT))
            return true;

        pml4addr = pml5e & address_mask;
    }

    uint64_t *pml4 = PHYS_TO_VIRT(pml4addr);

    const size_t first = PML4_INDEX(VM_MMIO_START);
    const size_t last = PML4_INDEX(VM_MMIO_END - 1);

    // Ensure all PML4 entries covering our MMIO region are absent.
    for (size_t i = first; i <= last; i++)
    {
        if (pml4[i] & PAGE_PRESENT)
            return false;
    }

    return true;
}

void *vm_map_mmio(uint64_t phys_addr, size_t size)
{
    // Reject mappings if the MMIO region could not be reserved.
    if (!mmio_region_available)
        return NULL;

    // Reject empty MMIO regions.
    if (size == 0)
        return NULL;

    // Determine the highest supported physical address.
    const uint64_t max_physical_address = (1ULL << maxphyaddr) - 1;

    // Reject physical addresses outside the supported range.
    if (phys_addr > max_physical_address)
        return NULL;

    // Ensure the entire requested region fits without overflow.
    if ((size - 1) > (max_physical_address - phys_addr))
        return NULL;

    // We have to account for some devices spanning over multiple pages.
    // Align the physical address down to a page boundary.
    const uint64_t alignment_mask = PAGE_SIZE - 1ULL;
    const uint64_t aligned_phys_addr = phys_addr & ~alignment_mask;

    // Calculate the offset within the first physical page.
    const uint64_t offset = phys_addr & alignment_mask;

    // Include the offset when calculating the required pages.
    const uint64_t total_size = offset + size;
    const uint64_t page_count = (total_size + alignment_mask) / PAGE_SIZE;

    // Calculate the required virtual address space.
    const uint64_t mapping_size = page_count * PAGE_SIZE;

    // Reject mappings that would exceed the MMIO region.
    if (mapping_size > (VM_MMIO_END - mmio_next_address))
        return NULL;

    // Device registers require uncacheable memory.
    uint64_t cache_flags;
    if (!vm_get_memory_type_flags(VM_MEMORY_UC, &cache_flags))
        return NULL;

    // Device registers must also be writable.
    const uint64_t mapping_flags = PAGE_RW | cache_flags;

    // Check every physical page for an existing HHDM mapping (aliases).
    for (uint64_t i = 0; i < page_count; i++)
    {
        const uint64_t phys =
            aligned_phys_addr + (i * PAGE_SIZE);

        // Prevent overflow when calculating the HHDM address.
        if (phys > UINT64_MAX - vmm_higher_half_offset)
            return NULL;

        const uint64_t hhdm_address =
            vmm_higher_half_offset + phys;

        uint64_t existing_phys_addr;

        // Reject existing direct mappings to avoid cache conflicts.
        if (vm_translate(hhdm_address, &existing_phys_addr))
            return NULL;
    }

    // Remember the starting virtual address of this mapping.
    const uint64_t virtual_base = mmio_next_address;

    // Map each physical page into the MMIO region.
    for (uint64_t i = 0; i < page_count; i++)
    {
        const uint64_t virt = virtual_base + (i * PAGE_SIZE);
        const uint64_t phys = aligned_phys_addr + (i * PAGE_SIZE);

        if (!vm_map_page(virt, phys, mapping_flags))
        {
            // Roll back any pages successfully mapped.
            for (uint64_t j = 0; j < i; j++)
                vm_unmap_page(virtual_base + (j * PAGE_SIZE));

            return NULL;
        }
    }

    // Advance the allocation cursor after a successful mapping.
    mmio_next_address += mapping_size;

    // Preserve the offset within the original physical page.
    // So this virtual address points to the very start of the physical
    // device memory within the page.
    return (void *)(virtual_base + offset);
}

void vm_init()
{
    kprintf("VMEM: Initializing...\n");

    vm_init_nx();

    mmio_next_address = VM_MMIO_START;
    
    if (is_paging_enabled()) {
        kprintf("VMEM: Paging enabled.\n");
    } else {
        kprintf("VMEM: Paging disabled.\n");
    }

    // Check what paging mode we're in (assuming 64-bit mode so no need to check IA32_EFER.LME).
    uint64_t cr4 = get_cr4();

    if (!(cr4 & CR4_PAE)) {
        kprintf("VMEM: 32-bit paging mode.\n");
    }

    if (cr4 & CR4_PAE && !(cr4 & CR4_LA57)) {
        kprintf("VMEM: 4-level paging mode.\n");
    }

    if (cr4 & CR4_PAE && cr4 & CR4_LA57) {
        kprintf("VMEM: 5-level paging mode.\n");
    }

    // Report address widths.
    uint64_t val = cpu_get_address_widths();
    maxphyaddr = val & 0xFF;
    maxlinaddr = val >> 8 & 0xFF;

    // Determine whether the reserved MMIO region is available.
    mmio_region_available = is_mmio_region_available();

    if (mmio_region_available)
        kprintf("VMEM: MMIO Virtual address region available.\n");
    else
        kprintf("VMEM: MMIO Virtual address region unavailable.\n");
}

/*
 * Returns the physical address of the currently active page-table root.
 *
 * CR3 may contain additional control bits, so these are masked out.
 */
uint64_t vm_get_address_space(void)
{
    const uint64_t address_mask = ((1ULL << maxphyaddr) - 1ULL) & ~(PAGE_SIZE - 1ULL);

    return get_cr3() & address_mask;
}

/*
 * Switches the current CPU to another address space.
 *
 * The caller must supply a valid page-table root containing all kernel
 * mappings needed to continue execution.
 *
 * This function does not validate the contents of the page tables.
 */
bool vm_switch_address_space(uint64_t page_table_root)
{
    if (maxphyaddr < 32 || maxphyaddr >= 64) {
        return false;
    }

    const uint64_t address_mask = ((1ULL << maxphyaddr) - 1ULL) & ~(PAGE_SIZE - 1ULL);

    if (page_table_root == 0 ||
        (page_table_root & (PAGE_SIZE - 1ULL)) != 0 ||
        (page_table_root & ~address_mask) != 0) {
        return false;
    }

    asm volatile (
        "mov %0, %%cr3"
        :
        : "r"(page_table_root)
        : "memory"
    );

    return true;
}

/*
 * Translates a user-accessible virtual address to a physical address.
 *
 * Every paging level must be present and marked user-accessible.
 */
bool vm_translate_user(uint64_t virt_addr, uint64_t *phys_addr)
{
    if (phys_addr == NULL) {
        return false;
    }

    if (virt_addr < PAGE_SIZE || virt_addr >= 0x0000800000000000ULL) {
        return false;
    }

    if (!is_canonical_address(virt_addr)) {
        return false;
    }

    if (maxphyaddr < 32 || maxphyaddr >= 64) {
        return false;
    }

    const uint64_t address_mask = ((1ULL << maxphyaddr) - 1) & ~(PAGE_SIZE - 1ULL);
    const uint64_t required_flags = PAGE_PRESENT | PAGE_USER;

    uint64_t table_address = get_cr3() & address_mask;

    if (get_cr4() & CR4_LA57) {
        uint64_t *pml5 = PHYS_TO_VIRT(table_address);
        uint64_t entry = pml5[PML5_INDEX(virt_addr)];

        if ((entry & required_flags) != required_flags) {
            return false;
        }

        table_address = entry & address_mask;
    }

    uint64_t *pml4 = PHYS_TO_VIRT(table_address);
    uint64_t pml4e = pml4[PML4_INDEX(virt_addr)];

    if ((pml4e & required_flags) != required_flags) {
        return false;
    }

    uint64_t *pdpt = PHYS_TO_VIRT(pml4e & address_mask);
    uint64_t pdpte = pdpt[PDPT_INDEX(virt_addr)];

    if ((pdpte & required_flags) != required_flags) {
        return false;
    }

    if (pdpte & PAGE_PS) {
        const uint64_t offset_mask = PAGE_SIZE_1GB - 1;
        *phys_addr = (pdpte & address_mask & ~offset_mask) | (virt_addr & offset_mask);
        return true;
    }

    uint64_t *pd = PHYS_TO_VIRT(pdpte & address_mask);
    uint64_t pde = pd[PD_INDEX(virt_addr)];

    if ((pde & required_flags) != required_flags) {
        return false;
    }

    if (pde & PAGE_PS) {
        const uint64_t offset_mask = PAGE_SIZE_2MB - 1;
        *phys_addr = (pde & address_mask & ~offset_mask) | (virt_addr & offset_mask);
        return true;
    }

    uint64_t *pt = PHYS_TO_VIRT(pde & address_mask);
    uint64_t pte = pt[PT_INDEX(virt_addr)];

    if ((pte & required_flags) != required_flags) {
        return false;
    }

    *phys_addr = (pte & address_mask) | PAGE_OFFSET(virt_addr);
    return true;
}