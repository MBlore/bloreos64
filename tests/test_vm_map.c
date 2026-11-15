#include "unity.h"
#include <vm.h>
#include "vm_test_helpers.h"

void test_vm_map_unaligned_virtual_address(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE + 1;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));
}

void test_vm_map_unaligned_physical_address(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = (8 * PAGE_SIZE) + 1;

    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));
}

void test_vm_map_noncanonical_virtual_address(void)
{
    vm_init();

    // Invalid canonical address in four-level paging.
    const uint64_t virtual_address = 0x0000800000000000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));
}

void test_vm_map_invalid_physical_address(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // First physical address beyond the supported address width.
    const uint64_t physical_address =
        1ULL << TEST_PHYSICAL_ADDRESS_WIDTH;

    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));
}

void test_vm_map_4kb_page(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Construct the existing paging hierarchy.
    link_4level_page_tables();

    // Create the virtual-to-physical mapping.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify the contents of the page table entry.
    TEST_ASSERT_EQUAL_UINT64(
        physical_address | TEST_PAGE_PRESENT | TEST_PAGE_RW,
        pt[1]
    );

    // Verify that the new mapping can be translated.
    assert_translation(virtual_address, physical_address);
}

void test_vm_map_existing_mapping(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t original_physical_address = 8 * PAGE_SIZE;
    const uint64_t new_physical_address = 9 * PAGE_SIZE;

    link_4level_page_tables();

    // Create the original mapping.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        original_physical_address,
        TEST_PAGE_RW
    ));

    // Attempt to overwrite the existing mapping.
    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        new_physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the original page table entry is unchanged.
    TEST_ASSERT_EQUAL_UINT64(
        original_physical_address | TEST_PAGE_PRESENT | TEST_PAGE_RW,
        pt[1]
    );

    assert_translation(
        virtual_address,
        original_physical_address
    );
}

void test_vm_map_inside_2mb_page(void)
{
    vm_init();

    const uint64_t virtual_address = TEST_PAGE_SIZE_2MB;
    const uint64_t physical_address = 8 * PAGE_SIZE;
    const uint64_t large_page_base = 4 * TEST_PAGE_SIZE_2MB;

    // Construct an existing 2 MiB page mapping.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    const uint64_t original_entry =
        large_page_base |
        TEST_PAGE_PRESENT |
        TEST_PAGE_RW |
        TEST_PAGE_SIZE_FLAG;

    pd[1] = original_entry;

    // Attempt to create a 4 KiB mapping inside the large page.
    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address + PAGE_SIZE,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the original mapping remains intact.
    TEST_ASSERT_EQUAL_UINT64(original_entry, pd[1]);

    assert_translation(
        virtual_address + PAGE_SIZE,
        large_page_base + PAGE_SIZE
    );
}

void test_vm_map_inside_1gb_page(void)
{
    vm_init();

    const uint64_t virtual_address = TEST_PAGE_SIZE_1GB;
    const uint64_t physical_address = 8 * PAGE_SIZE;
    const uint64_t huge_page_base = 4 * TEST_PAGE_SIZE_1GB;

    // Construct an existing 1 GiB page mapping.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    const uint64_t original_entry =
        huge_page_base |
        TEST_PAGE_PRESENT |
        TEST_PAGE_RW |
        TEST_PAGE_SIZE_FLAG;

    pdpt[1] = original_entry;

    // Attempt to create a 4 KiB mapping inside the huge page.
    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address + PAGE_SIZE,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the original mapping remains intact.
    TEST_ASSERT_EQUAL_UINT64(original_entry, pdpt[1]);

    assert_translation(
        virtual_address + PAGE_SIZE,
        huge_page_base + PAGE_SIZE
    );
}

void test_vm_map_missing_pt(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Construct the hierarchy without a final page table.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT | TEST_PAGE_RW;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT | TEST_PAGE_RW;

    // Verify that the page table does not yet exist.
    TEST_ASSERT_EQUAL_UINT64(0, pd[0]);

    // Create a mapping that requires a new page table.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the new page table is present.
    TEST_ASSERT_TRUE(pd[0] & TEST_PAGE_PRESENT);

    // Verify that the new mapping works.
    assert_translation(virtual_address, physical_address);
}

void test_vm_map_missing_pd(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Construct a hierarchy containing only PML4 and PDPT.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT | TEST_PAGE_RW;

    // Verify that the Page Directory does not exist.
    TEST_ASSERT_EQUAL_UINT64(0, pdpt[0]);

    // Create a mapping requiring two new page tables.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the new Page Directory is present.
    TEST_ASSERT_TRUE(pdpt[0] & TEST_PAGE_PRESENT);

    // Verify the completed mapping.
    assert_translation(virtual_address, physical_address);
}

void test_vm_map_missing_pdpt(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Verify that the PML4 has no PDPT linked to it.
    TEST_ASSERT_EQUAL_UINT64(0, pml4[0]);

    // Create a mapping requiring three new page tables.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the new PDPT is present.
    TEST_ASSERT_TRUE(pml4[0] & TEST_PAGE_PRESENT);

    // Verify the completed mapping.
    assert_translation(virtual_address, physical_address);
}

void test_vm_map_allocation_failure(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Exhaust all available physical memory.
    TEST_ASSERT_NOT_NULL(kpalloc(TEST_ALLOCATABLE_PAGE_COUNT));

    // Attempt to create a mapping requiring new page tables.
    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the PML4 entry remains unchanged.
    TEST_ASSERT_EQUAL_UINT64(0, pml4[0]);

    // Verify that no mapping was created.
    assert_translation_failure(virtual_address);
}

void test_vm_map_partial_allocation_failure(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Leave only one physical page available.
    TEST_ASSERT_NOT_NULL(
        kpalloc(TEST_ALLOCATABLE_PAGE_COUNT - 1)
    );

    // Creating this mapping requires three physical pages.
    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the original paging hierarchy is unchanged.
    TEST_ASSERT_EQUAL_UINT64(0, pml4[0]);

    // Verify that no mapping was created.
    assert_translation_failure(virtual_address);

    // Verify that the allocated physical page was released.
    TEST_ASSERT_NOT_NULL(kpalloc(1));
}

void test_vm_map_multiple_allocation_rollback(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;
    const size_t available_pages = 2;

    // Leave only two physical pages available.
    TEST_ASSERT_NOT_NULL(
        kpalloc(TEST_ALLOCATABLE_PAGE_COUNT - available_pages)
    );

    // Creating this mapping requires three physical pages.
    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the original paging hierarchy is restored.
    TEST_ASSERT_EQUAL_UINT64(0, pml4[0]);

    // Verify that no mapping was created.
    assert_translation_failure(virtual_address);

    // Verify that both allocated pages have been released.
    TEST_ASSERT_NOT_NULL(kpalloc(available_pages));
}

void test_vm_map_invalid_flags(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Physical address bits must not be supplied as flags.
    const uint64_t invalid_flags = PAGE_SIZE;

    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        invalid_flags
    ));

    // Invalid flags must not modify the paging hierarchy.
    TEST_ASSERT_EQUAL_UINT64(0, pml4[0]);

    assert_translation_failure(virtual_address);
}

void test_vm_map_readonly_parent(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    link_4level_page_tables();

    // Allow writing at all intermediate levels initially.
    pml4[0] |= TEST_PAGE_RW;
    pdpt[0] |= TEST_PAGE_RW;
    pd[0] |= TEST_PAGE_RW;

    // Make the PDPT entry read-only.
    pdpt[0] &= ~(uint64_t)TEST_PAGE_RW;

    const uint64_t original_entry = pdpt[0];

    // Reject writable mappings beneath a read-only parent.
    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the parent's permissions have not changed.
    TEST_ASSERT_EQUAL_UINT64(original_entry, pdpt[0]);

    // Verify that no mapping was created.
    assert_translation_failure(virtual_address);
}

void test_vm_map_supervisor_parent(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    link_4level_page_tables();

    // Allow user-mode access at every intermediate level.
    pml4[0] |= TEST_PAGE_USER;
    pdpt[0] |= TEST_PAGE_USER;
    pd[0] |= TEST_PAGE_USER;

    // Restrict the PDPT entry to supervisor-mode access.
    pdpt[0] &= ~(uint64_t)TEST_PAGE_USER;

    const uint64_t original_entry = pdpt[0];

    // Reject user-mode mappings beneath a supervisor-only parent.
    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW | TEST_PAGE_USER
    ));

    // Verify that the parent's permissions remain unchanged.
    TEST_ASSERT_EQUAL_UINT64(original_entry, pdpt[0]);

    // Verify that no mapping was created.
    assert_translation_failure(virtual_address);
}

void test_vm_map_user_missing_page_tables(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;
    const uint64_t flags = TEST_PAGE_RW | TEST_PAGE_USER;

    // Start with an empty paging hierarchy.
    TEST_ASSERT_EQUAL_UINT64(0, pml4[0]);

    // Create a user-accessible mapping requiring three new tables.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        flags
    ));

    // Verify that the PML4 entry permits user-mode access.
    TEST_ASSERT_TRUE(pml4[0] & TEST_PAGE_USER);

    // Verify that the mapping can be translated.
    assert_translation(virtual_address, physical_address);
}

void test_vm_map_readonly_page(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    link_4level_page_tables();

    // Create a read-only, supervisor-only mapping.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        0
    ));

    // Verify that only the present flag is set.
    TEST_ASSERT_EQUAL_UINT64(
        physical_address | TEST_PAGE_PRESENT,
        pt[1]
    );

    assert_translation(virtual_address, physical_address);
}

void test_vm_map_5level_existing_tables(void)
{
    setup_5level_paging();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Construct an existing five-level paging hierarchy.
    link_5level_page_tables();

    // Allow writing at the fifth level.
    pml5[0] |= TEST_PAGE_RW;

    // Create a mapping using five-level paging.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify the final page table entry.
    TEST_ASSERT_EQUAL_UINT64(
        physical_address | TEST_PAGE_PRESENT | TEST_PAGE_RW,
        pt[1]
    );

    assert_translation(virtual_address, physical_address);
}

void test_vm_map_5level_missing_tables(void)
{
    setup_5level_paging();

    // Use a virtual address beyond the four-level canonical range.
    const uint64_t virtual_address = 1ULL << 48;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Verify that the required PML5 entry is empty.
    TEST_ASSERT_EQUAL_UINT64(0, pml5[1]);

    // Create a mapping requiring four new intermediate tables.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the new PML4 is linked and writable.
    TEST_ASSERT_TRUE(pml5[1] & TEST_PAGE_PRESENT);
    TEST_ASSERT_TRUE(pml5[1] & TEST_PAGE_RW);

    // Verify that the completed mapping works.
    assert_translation(virtual_address, physical_address);
}

void test_vm_map_5level_allocation_rollback(void)
{
    setup_5level_paging();

    const uint64_t virtual_address = 1ULL << 48;
    const uint64_t physical_address = 32 * PAGE_SIZE;
    const size_t available_pages = 3;

    // Leave only three physical pages available.
    TEST_ASSERT_NOT_NULL(
        kpalloc(TEST_ALLOCATABLE_PAGE_COUNT - available_pages)
    );

    // Creating this mapping requires four new tables.
    TEST_ASSERT_FALSE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the original PML5 entry is restored.
    TEST_ASSERT_EQUAL_UINT64(0, pml5[1]);

    // Verify that no mapping was created.
    assert_translation_failure(virtual_address);

    // Verify that rollback invalidated cached paging information.
    TEST_ASSERT_EQUAL_UINT64(1, mock_invlpg_count);

    // Verify that the correct virtual address was invalidated.
    TEST_ASSERT_EQUAL_UINT64(
        virtual_address,
        mock_invlpg_address
    );

    // Verify that all three allocated pages were released.
    TEST_ASSERT_NOT_NULL(kpalloc(available_pages));
}

void test_vm_map_tlb_invalidation(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    link_4level_page_tables();

    // Verify that no invalidation has occurred yet.
    TEST_ASSERT_EQUAL_UINT64(0, mock_invlpg_count);

    // Create a new mapping.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that exactly one TLB invalidation occurred.
    TEST_ASSERT_EQUAL_UINT64(1, mock_invlpg_count);

    // Verify that the correct virtual address was invalidated.
    TEST_ASSERT_EQUAL_UINT64(
        virtual_address,
        mock_invlpg_address
    );
}

void test_vm_map_cache_flags(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    const uint64_t cache_flags =
        PAGE_PAT | PAGE_PCD | PAGE_PWT;

    // Create a mapping with all cache selection bits set.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        PAGE_RW | cache_flags
    ));

    // Locate the newly allocated paging structures.
    uint64_t *new_pdpt = page_tables[TEST_RESERVED_PAGE_COUNT];
    uint64_t *new_pd = page_tables[TEST_RESERVED_PAGE_COUNT + 1];
    uint64_t *new_pt = page_tables[TEST_RESERVED_PAGE_COUNT + 2];

    // Parent entries must not inherit the cache flags.
    TEST_ASSERT_EQUAL_HEX64(0, pml4[0] & cache_flags);
    TEST_ASSERT_EQUAL_HEX64(0, new_pdpt[0] & cache_flags);
    TEST_ASSERT_EQUAL_HEX64(0, new_pd[0] & cache_flags);

    // Only the leaf entry should contain the cache flags.
    TEST_ASSERT_EQUAL_HEX64(
        physical_address | PAGE_RW | TEST_PAGE_PRESENT | cache_flags,
        new_pt[1]
    );

    assert_translation(virtual_address, physical_address);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vm_map_unaligned_virtual_address);
    RUN_TEST(test_vm_map_unaligned_physical_address);
    RUN_TEST(test_vm_map_noncanonical_virtual_address);
    RUN_TEST(test_vm_map_invalid_physical_address);
    RUN_TEST(test_vm_map_4kb_page);
    RUN_TEST(test_vm_map_existing_mapping);
    RUN_TEST(test_vm_map_inside_2mb_page);
    RUN_TEST(test_vm_map_inside_1gb_page);
    RUN_TEST(test_vm_map_missing_pt);
    RUN_TEST(test_vm_map_missing_pd);
    RUN_TEST(test_vm_map_missing_pdpt);
    RUN_TEST(test_vm_map_allocation_failure);
    RUN_TEST(test_vm_map_partial_allocation_failure);
    RUN_TEST(test_vm_map_multiple_allocation_rollback);
    RUN_TEST(test_vm_map_invalid_flags);
    RUN_TEST(test_vm_map_readonly_parent);
    RUN_TEST(test_vm_map_supervisor_parent);
    RUN_TEST(test_vm_map_user_missing_page_tables);
    RUN_TEST(test_vm_map_readonly_page);
    RUN_TEST(test_vm_map_5level_existing_tables);
    RUN_TEST(test_vm_map_5level_missing_tables);
    RUN_TEST(test_vm_map_5level_allocation_rollback);
    RUN_TEST(test_vm_map_tlb_invalidation);
    RUN_TEST(test_vm_map_cache_flags);

    return UNITY_END();
}