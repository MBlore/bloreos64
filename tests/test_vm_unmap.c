#include "unity.h"
#include <vm.h>
#include "vm_test_helpers.h"

void test_vm_unmap_missing_mapping(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // Attempt to remove a mapping that does not exist.
    TEST_ASSERT_FALSE(vm_unmap_page(virtual_address));
}

void test_vm_unmap_noncanonical_address(void)
{
    vm_init();

    // Invalid canonical address in four-level paging.
    const uint64_t virtual_address = 0x0000800000000000ULL;

    TEST_ASSERT_FALSE(vm_unmap_page(virtual_address));
}

void test_vm_unmap_unaligned_address(void)
{
    vm_init();

    link_4level_page_tables();

    // Create an existing mapping.
    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Attempt to unmap using an unaligned address.
    TEST_ASSERT_FALSE(vm_unmap_page(virtual_address + 1));

    // Verify that the original mapping remains intact.
    assert_translation(virtual_address, physical_address);
}

void test_vm_unmap_4level_mapping(void)
{
    vm_init();

    link_4level_page_tables();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Create a writable virtual memory mapping.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Verify that the mapping exists.
    assert_translation(virtual_address, physical_address);

    // Remove the mapping.
    TEST_ASSERT_TRUE(vm_unmap_page(virtual_address));

    // Verify that the address can no longer be translated.
    assert_translation_failure(virtual_address);
}

void test_vm_unmap_2mb_mapping(void)
{
    vm_init();

    link_4level_page_tables();

    // Establish a 2 MiB mapping in the page directory.
    pd[0] = TEST_PAGE_SIZE_2MB |
            TEST_PAGE_PRESENT |
            TEST_PAGE_SIZE_FLAG;

    // Attempt to unmap a 4 KiB page within the large page.
    TEST_ASSERT_FALSE(vm_unmap_page(PAGE_SIZE));

    // Verify that the large page mapping remains intact.
    assert_translation(
        PAGE_SIZE,
        TEST_PAGE_SIZE_2MB + PAGE_SIZE
    );
}

void test_vm_unmap_1gb_mapping(void)
{
    vm_init();

    link_4level_page_tables();

    // Establish a 1 GiB mapping in the PDPT.
    pdpt[0] = TEST_PAGE_SIZE_1GB |
              TEST_PAGE_PRESENT |
              TEST_PAGE_SIZE_FLAG;

    // Attempt to unmap a 4 KiB page within the large page.
    TEST_ASSERT_FALSE(vm_unmap_page(PAGE_SIZE));

    // Verify that the large page mapping remains intact.
    assert_translation(
        PAGE_SIZE,
        TEST_PAGE_SIZE_1GB + PAGE_SIZE
    );
}

void test_vm_unmap_5level_mapping(void)
{
    setup_5level_paging();
    link_5level_page_tables();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Establish a 4 KiB mapping under five-level paging.
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    // Verify that the mapping exists.
    assert_translation(virtual_address, physical_address);

    // Remove the mapping.
    TEST_ASSERT_TRUE(vm_unmap_page(virtual_address));

    // Verify that the mapping no longer exists.
    assert_translation_failure(virtual_address);
}

void test_vm_unmap_preserves_neighbouring_mapping(void)
{
    vm_init();

    link_4level_page_tables();

    const uint64_t virtual_address_a = PAGE_SIZE;
    const uint64_t virtual_address_b = 2 * PAGE_SIZE;

    const uint64_t physical_address_a = 32 * PAGE_SIZE;
    const uint64_t physical_address_b = 33 * PAGE_SIZE;

    // Establish two neighbouring mappings.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address_a,
        physical_address_a,
        TEST_PAGE_RW
    ));

    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address_b,
        physical_address_b,
        TEST_PAGE_RW
    ));

    // Remove the first mapping.
    TEST_ASSERT_TRUE(vm_unmap_page(virtual_address_a));

    // Verify that only the requested mapping was removed.
    assert_translation_failure(virtual_address_a);
    assert_translation(virtual_address_b, physical_address_b);
}

void test_vm_unmap_tlb_invalidation(void)
{
    vm_init();

    link_4level_page_tables();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 32 * PAGE_SIZE;

    // Establish an existing mapping.
    TEST_ASSERT_TRUE(vm_map_page(
        virtual_address,
        physical_address,
        TEST_PAGE_RW
    ));

    // Reset the TLB invalidation state after mapping.
    mock_invlpg_count = 0;
    mock_invlpg_address = 0;

    // Remove the mapping.
    TEST_ASSERT_TRUE(vm_unmap_page(virtual_address));

    // Verify that the correct virtual address was invalidated.
    TEST_ASSERT_EQUAL_UINT64(1, mock_invlpg_count);
    TEST_ASSERT_EQUAL_UINT64(
        virtual_address,
        mock_invlpg_address
    );

    // Attempt to remove the same mapping again.
    TEST_ASSERT_FALSE(vm_unmap_page(virtual_address));

    // A failed unmapping must not invalidate the TLB again.
    TEST_ASSERT_EQUAL_UINT64(1, mock_invlpg_count);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vm_unmap_missing_mapping);
    RUN_TEST(test_vm_unmap_unaligned_address);
    RUN_TEST(test_vm_unmap_noncanonical_address);
    RUN_TEST(test_vm_unmap_4level_mapping);
    RUN_TEST(test_vm_unmap_2mb_mapping);
    RUN_TEST(test_vm_unmap_1gb_mapping);
    RUN_TEST(test_vm_unmap_5level_mapping);
    RUN_TEST(test_vm_unmap_preserves_neighbouring_mapping);
    RUN_TEST(test_vm_unmap_tlb_invalidation);

    return UNITY_END();
}