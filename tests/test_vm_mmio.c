#include "unity.h"
#include <vm.h>
#include "vm_test_helpers.h"
#include "mocks/cpu.h"

void test_vm_mmio_zero_size(void)
{
    vm_init();

    const uint64_t physical_address = 0xF0000000ULL;

    TEST_ASSERT_NULL(vm_map_mmio(physical_address, 0));

    // No paging structures should have been modified.
    TEST_ASSERT_EQUAL_UINT64(0, mock_invlpg_count);
}

void test_vm_mmio_invalid_physical_address(void)
{
    vm_init();

    // First address beyond the CPU's physical addressing limit.
    const uint64_t physical_address =
        1ULL << TEST_PHYSICAL_ADDRESS_WIDTH;

    TEST_ASSERT_NULL(vm_map_mmio(physical_address, PAGE_SIZE));

    TEST_ASSERT_EQUAL_UINT64(0, mock_invlpg_count);
}

void test_vm_mmio_physical_range_overflow(void)
{
    vm_init();

    // Start at the last supported physical page.
    const uint64_t physical_address =
        (1ULL << TEST_PHYSICAL_ADDRESS_WIDTH) - PAGE_SIZE;

    // Request two pages, exceeding the physical address limit.
    TEST_ASSERT_NULL(vm_map_mmio(physical_address, 2 * PAGE_SIZE));

    TEST_ASSERT_EQUAL_UINT64(0, mock_invlpg_count);
}

void test_vm_mmio_single_page(void)
{
    vm_init();

    const uint64_t physical_address = 0xF0000000ULL;
    const uint64_t expected_virtual_address = 0xFFFFC00000000000ULL;

    void *address = vm_map_mmio(physical_address, PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(address);

    TEST_ASSERT_EQUAL_UINT64(
        expected_virtual_address, (uint64_t)address);

    assert_translation(
        (uint64_t)address, physical_address);

    TEST_ASSERT_EQUAL_UINT64(1, mock_invlpg_count);
}

void test_vm_mmio_multiple_pages(void)
{
    vm_init();

    const uint64_t physical_address = 0xF0000000ULL;
    const uint64_t page_count = 3;

    void *address = vm_map_mmio(
        physical_address, page_count * PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(address);

    const uint64_t virtual_address = (uint64_t)address;

    // Verify that every page maps to the correct physical address.
    for (uint64_t i = 0; i < page_count; i++)
    {
        assert_translation(
            virtual_address + (i * PAGE_SIZE),
            physical_address + (i * PAGE_SIZE));
    }

    // Verify that no additional page was mapped.
    assert_translation_failure(
        virtual_address + (page_count * PAGE_SIZE));

    TEST_ASSERT_EQUAL_UINT64(page_count, mock_invlpg_count);
}

void test_vm_mmio_unaligned_address(void)
{
    vm_init();

    const uint64_t physical_base = 0xF0000000ULL;
    const uint64_t offset = PAGE_SIZE / 2;

    const uint64_t physical_address = physical_base + offset;
    const uint64_t virtual_base = 0xFFFFC00000000000ULL;

    void *address = vm_map_mmio(physical_address, PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(address);

    // Verify that the returned pointer preserves the offset.
    TEST_ASSERT_EQUAL_UINT64(
        virtual_base + offset, (uint64_t)address);

    // Verify that both required pages are mapped.
    assert_translation(virtual_base, physical_base);

    assert_translation(
        virtual_base + PAGE_SIZE,
        physical_base + PAGE_SIZE);

    // Verify the returned address itself.
    assert_translation((uint64_t)address, physical_address);

    // Ensure no unnecessary third page was mapped.
    assert_translation_failure(virtual_base + 2 * PAGE_SIZE);

    TEST_ASSERT_EQUAL_UINT64(2, mock_invlpg_count);
}

void test_vm_mmio_consecutive_allocations(void)
{
    vm_init();

    const uint64_t first_phys_addr = 0xF0000000ULL;
    const uint64_t second_phys_addr = 0xF1000000ULL;

    const uint64_t first_size = 2 * PAGE_SIZE;

    void *first = vm_map_mmio(first_phys_addr, first_size);
    void *second = vm_map_mmio(second_phys_addr, PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);

    // The second mapping must begin after the first mapping.
    TEST_ASSERT_EQUAL_UINT64(
        (uint64_t)first + first_size,
        (uint64_t)second);

    // Verify that both mappings remain intact.
    assert_translation((uint64_t)first, first_phys_addr);

    assert_translation(
        (uint64_t)first + PAGE_SIZE,
        first_phys_addr + PAGE_SIZE);

    assert_translation((uint64_t)second, second_phys_addr);

    TEST_ASSERT_EQUAL_UINT64(3, mock_invlpg_count);
}

void test_vm_mmio_cache_flags(void)
{
    vm_init();

    const uint64_t physical_address = 0xF0000000ULL;

    void *address = vm_map_mmio(physical_address, PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(address);

    // Locate the newly allocated page table.
    uint64_t *new_pt =
        page_tables[TEST_RESERVED_PAGE_COUNT + 2];

    // The default PAT configuration selects UC at index 3.
    const uint64_t expected_flags =
        TEST_PAGE_PRESENT | PAGE_RW | PAGE_PWT | PAGE_PCD;

    // Verify the complete page table entry.
    TEST_ASSERT_EQUAL_UINT64(
        physical_address | expected_flags,
        new_pt[0]);
}

void test_vm_mmio_rollback(void)
{
    vm_init();

    const uint64_t virtual_base = 0xFFFFC00000000000ULL;
    const uint64_t physical_base = 0xF0000000ULL;
    const uint64_t existing_phys_addr = 0xF1000000ULL;

    // Intentionally occupy the second virtual page.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_base + PAGE_SIZE,
        existing_phys_addr,
        PAGE_RW
    ));

    // Attempt to map three pages, causing a conflict.
    TEST_ASSERT_NULL(
        vm_map_mmio(physical_base, 3 * PAGE_SIZE)
    );

    // Verify that the first mapping was rolled back.
    assert_translation_failure(virtual_base);

    // Verify that the existing mapping was preserved.
    assert_translation(
        virtual_base + PAGE_SIZE,
        existing_phys_addr
    );

    // Verify that the third page was never mapped.
    assert_translation_failure(
        virtual_base + 2 * PAGE_SIZE
    );

    // Verify that the allocation cursor has not advanced.
    void *address = vm_map_mmio(physical_base, PAGE_SIZE);

    TEST_ASSERT_EQUAL_UINT64(
        virtual_base, (uint64_t)address
    );
}

void test_vm_mmio_allocation_failure(void)
{
    vm_init();

    const uint64_t virtual_base = 0xFFFFC00000000000ULL;
    const uint64_t physical_address = 0xF0000000ULL;
    const size_t available_pages = 2;

    // Reserve physical memory, leaving only two pages available.
    TEST_ASSERT_NOT_NULL(
        kpalloc(TEST_ALLOCATABLE_PAGE_COUNT - available_pages)
    );

    // Creating this mapping requires three new paging structures.
    TEST_ASSERT_NULL(
        vm_map_mmio(physical_address, PAGE_SIZE)
    );

    // Verify that no mapping was created.
    assert_translation_failure(virtual_base);

    // Verify that both allocated page tables were released.
    TEST_ASSERT_NOT_NULL(kpalloc(available_pages));
}

void test_vm_mmio_region_exhaustion(void)
{
    // Simulate a CPU supporting 48-bit physical addresses.
    const uint64_t physical_address_width = 48;

    mock_address_widths =
        physical_address_width |
        ((uint64_t)TEST_LINEAR_ADDRESS_WIDTH << 8);
    
    vm_init();

    const uint64_t physical_address = 0;
    const uint64_t region_size = 1ULL << 40;

    // Request more than the entire MMIO region.
    TEST_ASSERT_NULL(
        vm_map_mmio(physical_address, region_size + PAGE_SIZE)
    );

    // Verify that no TLB invalidations occurred.
    TEST_ASSERT_EQUAL_UINT64(0, mock_invlpg_count);

    // Verify that no physical memory was consumed.
    TEST_ASSERT_NOT_NULL(
        kpalloc(TEST_ALLOCATABLE_PAGE_COUNT)
    );
}

void test_vm_mmio_5level_paging(void)
{
    setup_5level_paging();

    const uint64_t physical_address = 0xF0000000ULL;
    const uint64_t virtual_base = 0xFFFFC00000000000ULL;

    void *address = vm_map_mmio(physical_address, PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(address);

    TEST_ASSERT_EQUAL_UINT64(
        virtual_base, (uint64_t)address);

    assert_translation(virtual_base, physical_address);

    TEST_ASSERT_EQUAL_UINT64(1, mock_invlpg_count);
}

void test_vm_mmio_region_unavailable(void)
{
    const uint64_t virtual_base = 0xFFFFC00000000000ULL;
    const uint64_t physical_address = 0xF0000000ULL;

    // Simulate an existing paging hierarchy within the MMIO region.
    const size_t pml4_index = (virtual_base >> 39) & 0x1FF;

    pml4[pml4_index] =
        TEST_PDPT_PHYSICAL_ADDRESS |
        TEST_PAGE_PRESENT |
        TEST_PAGE_RW;

    // Initialize virtual memory with an occupied MMIO region.
    vm_init();

    // The MMIO allocator must reject the request.
    TEST_ASSERT_NULL(
        vm_map_mmio(physical_address, PAGE_SIZE)
    );

    TEST_ASSERT_EQUAL_UINT64(0, mock_invlpg_count);
}

void test_vm_mmio_region_partially_occupied(void)
{
    const uint64_t virtual_base = 0xFFFFC00000000000ULL;
    const uint64_t physical_address = 0xF0000000ULL;

    // Each PML4 entry covers 512 GiB of virtual address space.
    const uint64_t pml4_region_size = 1ULL << 39;

    const uint64_t second_region =
        virtual_base + pml4_region_size;

    const size_t pml4_index = (second_region >> 39) & 0x1FF;

    // Occupy only the second PML4 entry.
    pml4[pml4_index] =
        TEST_PDPT_PHYSICAL_ADDRESS |
        TEST_PAGE_PRESENT |
        TEST_PAGE_RW;

    vm_init();

    TEST_ASSERT_NULL(
        vm_map_mmio(physical_address, PAGE_SIZE)
    );

    TEST_ASSERT_EQUAL_UINT64(0, mock_invlpg_count);
}

void test_vm_mmio_5level_region_unavailable(void)
{
    const uint64_t virtual_base = 0xFFFFC00000000000ULL;
    const uint64_t physical_address = 0xF0000000ULL;

    const size_t pml5_index = (virtual_base >> 48) & 0x1FF;
    const size_t pml4_index = (virtual_base >> 39) & 0x1FF;

    // Link the PML5 to an existing PML4.
    pml5[pml5_index] =
        TEST_PML4_PHYSICAL_ADDRESS |
        TEST_PAGE_PRESENT |
        TEST_PAGE_RW;

    // Occupy the reserved MMIO region.
    pml4[pml4_index] =
        TEST_PDPT_PHYSICAL_ADDRESS |
        TEST_PAGE_PRESENT |
        TEST_PAGE_RW;

    // Initialize five-level paging.
    setup_5level_paging();

    // Verify that the MMIO reservation was rejected.
    TEST_ASSERT_NULL(
        vm_map_mmio(physical_address, PAGE_SIZE)
    );

    TEST_ASSERT_EQUAL_UINT64(0, mock_invlpg_count);
}

void test_vm_mmio_existing_hhdm_mapping(void)
{
    vm_init();

    const uint64_t physical_address = 0xF0000000ULL;

    const uint64_t hhdm_address =
        vmm_higher_half_offset + physical_address;

    // Simulate an existing direct memory mapping.
    TEST_ASSERT_TRUE(vm_map_page(
        hhdm_address,
        physical_address,
        PAGE_RW
    ));

    // Reject MMIO mappings to existing HHDM pages.
    TEST_ASSERT_NULL(
        vm_map_mmio(physical_address, PAGE_SIZE)
    );

    // Verify that the original mapping remains intact.
    assert_translation(hhdm_address, physical_address);
}

void test_vm_mmio_partial_hhdm_alias(void)
{
    vm_init();

    const uint64_t physical_address = 0xF0000000ULL;
    const uint64_t virtual_base = 0xFFFFC00000000000ULL;

    // Create an HHDM mapping for the second physical page.
    const uint64_t second_phys_addr =
        physical_address + PAGE_SIZE;

    const uint64_t hhdm_address =
        vmm_higher_half_offset + second_phys_addr;

    TEST_ASSERT_TRUE(vm_map_page(
        hhdm_address,
        second_phys_addr,
        PAGE_RW
    ));

    const size_t original_invlpg_count = mock_invlpg_count;

    // Reject the entire MMIO request.
    TEST_ASSERT_NULL(
        vm_map_mmio(physical_address, 3 * PAGE_SIZE)
    );

    // Verify that no MMIO mappings were created.
    for (size_t i = 0; i < 3; i++)
        assert_translation_failure(
            virtual_base + (i * PAGE_SIZE));

    // Verify that the existing mapping remains intact.
    assert_translation(hhdm_address, second_phys_addr);

    // Verify that no additional TLB invalidations occurred.
    TEST_ASSERT_EQUAL_UINT64(
        original_invlpg_count, mock_invlpg_count);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vm_mmio_zero_size);
    RUN_TEST(test_vm_mmio_invalid_physical_address);
    RUN_TEST(test_vm_mmio_physical_range_overflow);
    RUN_TEST(test_vm_mmio_single_page);
    RUN_TEST(test_vm_mmio_multiple_pages);
    RUN_TEST(test_vm_mmio_unaligned_address);
    RUN_TEST(test_vm_mmio_consecutive_allocations);
    RUN_TEST(test_vm_mmio_cache_flags);
    RUN_TEST(test_vm_mmio_rollback);
    RUN_TEST(test_vm_mmio_allocation_failure);
    RUN_TEST(test_vm_mmio_region_exhaustion);
    RUN_TEST(test_vm_mmio_5level_paging);
    RUN_TEST(test_vm_mmio_region_unavailable);
    RUN_TEST(test_vm_mmio_region_partially_occupied);
    RUN_TEST(test_vm_mmio_5level_region_unavailable);
    RUN_TEST(test_vm_mmio_existing_hhdm_mapping);
    RUN_TEST(test_vm_mmio_partial_hhdm_alias);

    return UNITY_END();
}