#include "vm_test_helpers.h"
#include "mocks/cpu.h"
#include "unity.h"
#include <vm.h>

// Simulated CPU state.
uint64_t mock_cr3;
uint64_t mock_cr4;
uint64_t mock_address_widths;
int mock_paging_enabled;
uint64_t mock_pat;
uint32_t mock_cpuid_edx;
uint32_t mock_read_msr_count;
uint64_t mock_efer = 0;

size_t mock_invlpg_count;
uint64_t mock_invlpg_address;

// Simulated higher-half direct memory mapping.
uint64_t vmm_higher_half_offset;

// Simulated physical memory containing page-aligned paging structures.
_Alignas(PAGE_SIZE)
uint64_t page_tables[TEST_PAGE_TABLE_COUNT][PAGE_TABLE_ENTRIES];

// Index of the next available simulated physical page.
static size_t mock_allocation_cursor;

void setUp(void)
{
    mock_read_msr_count = 0;
    
    // Simulate a CPU with PAT support by default.
    mock_cpuid_edx = CPUID_EDX_PAT;

    // Reset PAT to its architectural default configuration.
    mock_pat = 0x0007040600070406ULL;
    
    // Mark every entry in every page table as not present.
    for (size_t level = 0; level < TEST_PAGE_TABLE_COUNT; level++) {
        for (size_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
            page_tables[level][i] = 0;
        }
    }

    // Simulate four-level paging.
    mock_cr3 = TEST_PML4_PHYSICAL_ADDRESS;
    mock_cr4 = CR4_PAE;
    mock_paging_enabled = 1;

    mock_address_widths =
        TEST_PHYSICAL_ADDRESS_WIDTH |
        (TEST_LINEAR_ADDRESS_WIDTH << 8);

    // Translate simulated physical addresses into host memory.
    vmm_higher_half_offset =
        (uintptr_t)pml4 - TEST_PML4_PHYSICAL_ADDRESS;

    // Reset the allocator before each test.
    mock_allocation_cursor = TEST_RESERVED_PAGE_COUNT;

    // Reset simulated TLB invalidation state.
    mock_invlpg_count = 0;
    mock_invlpg_address = 0;
}

void tearDown(void)
{
}

void setup_5level_paging(void)
{
    // Simulate a CPU operating in five-level paging mode.
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH |
                          (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);
    vm_init();
}

void link_4level_page_tables(void)
{
    // Connect the four levels of the paging hierarchy.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT | TEST_PAGE_RW;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT | TEST_PAGE_RW;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT | TEST_PAGE_RW;
}

void link_5level_page_tables(void)
{
    pml5[0] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    link_4level_page_tables();
}

void assert_translation_failure(uint64_t virtual_address)
{
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void assert_translation(uint64_t virtual_address,
                               uint64_t expected_physical_address)
{
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(expected_physical_address, result);
}

void *kpalloc(size_t numPages)
{
    // Reject zero-sized allocations.
    if (numPages == 0) {
        return NULL;
    }

    // Reject allocations exceeding the remaining memory.
    if (numPages > TEST_PAGE_TABLE_COUNT - mock_allocation_cursor) {
        return NULL;
    }

    // Obtain the virtual address of the first available page.
    void *address = page_tables[mock_allocation_cursor];

    // Reserve the requested number of contiguous pages.
    mock_allocation_cursor += numPages;

    return address;
}

void kfree(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    // Never release reserved paging structures.
    if (mock_allocation_cursor <= TEST_RESERVED_PAGE_COUNT) {
        return;
    }

    // Release the most recently allocated page.
    if (ptr == (void *)page_tables[mock_allocation_cursor - 1]) {
        mock_allocation_cursor--;
    }
}

void invlpg(uint64_t virt_addr)
{
    mock_invlpg_count++;
    mock_invlpg_address = virt_addr;
}