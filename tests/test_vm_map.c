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

    const uint64_t virtual_address = TEST_LARGE_PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;
    const uint64_t large_page_base = 4 * TEST_LARGE_PAGE_SIZE;

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

    const uint64_t virtual_address = TEST_HUGE_PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;
    const uint64_t huge_page_base = 4 * TEST_HUGE_PAGE_SIZE;

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

    return UNITY_END();
}