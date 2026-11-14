#include "unity.h"
#include <stdint.h>
#include <stdbool.h>
#include <mem.h>
#include <vm.h>
#include "mocks/cpu.h"

#define PAGE_TABLE_ENTRIES 512

#define TEST_PHYSICAL_ADDRESS_WIDTH 40
#define TEST_LINEAR_ADDRESS_WIDTH 48

#define TEST_PML4_PHYSICAL_ADDRESS PAGE_SIZE
#define TEST_PDPT_PHYSICAL_ADDRESS (2 * PAGE_SIZE)
#define TEST_PD_PHYSICAL_ADDRESS   (3 * PAGE_SIZE)
#define TEST_PT_PHYSICAL_ADDRESS   (4 * PAGE_SIZE)
#define TEST_PML5_PHYSICAL_ADDRESS (5 * PAGE_SIZE)
#define TEST_PAGE_PRESENT 0x1ULL
#define TEST_PAGE_RW   0x2ULL
#define TEST_PAGE_USER 0x4ULL
#define TEST_PAGE_SIZE_FLAG (1ULL << 7)

#define TEST_LARGE_PAGE_SIZE (2ULL * 1024 * 1024) // 2 MiB
#define TEST_HUGE_PAGE_SIZE (1ULL * 1024 * 1024 * 1024) // 1 GiB

#define TEST_LINEAR_ADDRESS_WIDTH_5LEVEL 57ULL

// Simulated CPU state.
uint64_t mock_cr3;
uint64_t mock_cr4;
uint64_t mock_address_widths;
int mock_paging_enabled;

// Simulated higher-half direct memory mapping.
uint64_t vmm_higher_half_offset;

// Address widths maintained by the VMM.
extern uint32_t maxphyaddr;
extern uint32_t maxlinaddr;

#define TEST_PAGE_TABLE_COUNT 5

_Alignas(PAGE_SIZE)
static uint64_t page_tables[TEST_PAGE_TABLE_COUNT][PAGE_TABLE_ENTRIES];

#define pml4 page_tables[0]
#define pdpt page_tables[1]
#define pd   page_tables[2]
#define pt   page_tables[3]
#define pml5 page_tables[4]

void setUp(void)
{
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
}

void tearDown(void)
{
}

void test_vm_initialisation(void)
{
    vm_init();

    TEST_ASSERT_EQUAL_UINT32(
        TEST_PHYSICAL_ADDRESS_WIDTH, maxphyaddr);

    TEST_ASSERT_EQUAL_UINT32(
        TEST_LINEAR_ADDRESS_WIDTH, maxlinaddr);
}

void test_vm_translate_unmapped_address(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // No page table entries are present, so translation must fail.
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void test_vm_translate_4kb_page(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Link the four levels of the paging hierarchy.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Map the virtual page to a physical page.
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address,
        result);
}

void test_vm_translate_page_offset(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;
    const uint64_t offset = 0x234;

    // Link the four levels of the paging hierarchy.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    pt[1] = physical_address | TEST_PAGE_PRESENT;

    // Translation must preserve the offset within the page.
    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address + offset, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address + offset,
        result);
}

void test_vm_translate_entry_flags(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    const uint64_t flags =
        TEST_PAGE_PRESENT |
        TEST_PAGE_RW |
        TEST_PAGE_USER;

    // Include permission flags at every paging level.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | flags;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | flags;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | flags;
    pt[1] = physical_address | flags;

    // Translation must exclude the permission flags.
    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address,
        result);
}

void test_vm_translate_nonzero_indexes(void)
{
    vm_init();

    const uint64_t virtual_address = 0x0000008080604000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Link the tables using non-zero indexes at every level.
    pml4[1] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[2] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[3] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pt[4] = physical_address | TEST_PAGE_PRESENT;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address,
        result);
}

void test_vm_translate_higher_half_address(void)
{
    vm_init();

    const uint64_t virtual_address = 0xFFFF800000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // PML4 index 256 is the start of the higher half.
    pml4[256] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address,
        result);
}

void test_vm_translate_missing_pdpt_entry(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // Link the PML4 to the PDPT.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // The PDPT entry remains empty.

    // Translation must stop at the missing entry.
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void test_vm_translate_missing_pd_entry(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // Link the PML4 and PDPT tables.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // The PD entry remains empty.

    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void test_vm_translate_missing_pt_entry(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // Link all four levels of the paging hierarchy.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // The final PT entry remains empty.

    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void test_vm_translate_2mb_page(void)
{
    vm_init();

    const uint64_t virtual_address = TEST_LARGE_PAGE_SIZE;
    const uint64_t physical_address = 0;

    // Link the PML4 and PDPT tables.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Map a 2 MiB page directly from the Page Directory.
    pd[1] = physical_address |
            TEST_PAGE_PRESENT |
            TEST_PAGE_SIZE_FLAG;

    const uint64_t offset = 0x1234;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address + offset, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address + offset,
        result);
}

void test_vm_translate_1gb_page(void)
{
    vm_init();

    const uint64_t virtual_address = TEST_HUGE_PAGE_SIZE;
    const uint64_t physical_address = 0;
    const uint64_t offset = 0x12345678;

    // Link the PML4 to the PDPT.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Map a 1 GiB page directly from the PDPT.
    pdpt[1] = physical_address |
              TEST_PAGE_PRESENT |
              TEST_PAGE_SIZE_FLAG;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address + offset, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address + offset,
        result);
}

void test_vm_translate_2mb_nonzero_base(void)
{
    vm_init();

    const uint64_t virtual_address = TEST_LARGE_PAGE_SIZE;
    const uint64_t physical_address = 4 * TEST_LARGE_PAGE_SIZE;
    const uint64_t offset = 0x123456;

    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Map a 2 MiB page to a non-zero physical address.
    pd[1] = physical_address |
            TEST_PAGE_PRESENT |
            TEST_PAGE_SIZE_FLAG;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address + offset, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address + offset,
        result);
}

void test_vm_translate_1gb_nonzero_base(void)
{
    vm_init();

    const uint64_t virtual_address = TEST_HUGE_PAGE_SIZE;
    const uint64_t physical_address = 4 * TEST_HUGE_PAGE_SIZE;
    const uint64_t offset = 0x12345678;

    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Map a 1 GiB page to a non-zero physical address.
    pdpt[1] = physical_address |
              TEST_PAGE_PRESENT |
              TEST_PAGE_SIZE_FLAG;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address + offset, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address + offset,
        result);
}

void test_vm_translate_noncanonical_address(void)
{
    vm_init();

    const uint64_t virtual_address = 0x0000800000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Create a valid mapping in the higher half.
    pml4[256] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    // A non-canonical virtual address must not be translated.
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void test_vm_translate_noncanonical_lower_half(void)
{
    vm_init();

    const uint64_t virtual_address = 0xFFFF000000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Create a valid mapping in the lower half.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    // Reject incorrect sign extension of bit 47.
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void test_vm_translate_5level_missing_pd_entry(void)
{
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH |
                          (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);

    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // Link the first three levels, leaving the PD entry empty.
    pml5[0] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void test_vm_translate_5level_4kb_page(void)
{
    // Simulate active five-level paging.
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH | (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);

    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Link all five levels of the paging hierarchy.
    pml5[0] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address,
        result);
}

void test_vm_translate_5level_extended_address(void)
{
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH |
                          (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);

    vm_init();

    const uint64_t virtual_address = 0x0001000000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Use a non-zero PML5 index.
    pml5[1] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address,
        result);
}

void test_vm_translate_5level_higher_half(void)
{
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH |
                          (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);

    vm_init();

    const uint64_t virtual_address = 0xFF00000000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Map an address in the five-level higher half.
    pml5[256] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address,
        result);
}

void test_vm_translate_5level_noncanonical_address(void)
{
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH |
                          (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);

    vm_init();

    const uint64_t virtual_address = 0x0100000000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Create a valid mapping in the higher half.
    pml5[256] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    // Reject an incorrectly sign-extended virtual address.
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void test_vm_translate_missing_pml5_entry(void)
{
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH |
                          (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);

    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // Leave the PML5 entry empty.
    // The walker must terminate at this level.
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

void test_vm_translate_5level_2mb_page(void)
{
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH |
                          (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);

    vm_init();

    const uint64_t virtual_address = TEST_LARGE_PAGE_SIZE;
    const uint64_t physical_address = 4 * TEST_LARGE_PAGE_SIZE;
    const uint64_t offset = 0x123456;

    pml5[0] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Terminate the walk at the Page Directory.
    pd[1] = physical_address |
            TEST_PAGE_PRESENT |
            TEST_PAGE_SIZE_FLAG;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address + offset, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address + offset,
        result);
}

void test_vm_translate_5level_1gb_page(void)
{
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH |
                          (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);

    vm_init();

    const uint64_t virtual_address = TEST_HUGE_PAGE_SIZE;
    const uint64_t physical_address = 4 * TEST_HUGE_PAGE_SIZE;
    const uint64_t offset = 0x12345678;

    pml5[0] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Terminate the walk at the PDPT.
    pdpt[1] = physical_address |
              TEST_PAGE_PRESENT |
              TEST_PAGE_SIZE_FLAG;

    uint64_t result = 0;
    TEST_ASSERT_TRUE(vm_translate(virtual_address + offset, &result));
    TEST_ASSERT_EQUAL_UINT64(
        physical_address + offset,
        result);
}

void test_vm_translate_physical_address_zero(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 0;

    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Physical address zero is a valid mapping.
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    uint64_t result = UINT64_MAX;

    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(physical_address, result);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vm_initialisation);
    RUN_TEST(test_vm_translate_unmapped_address);
    RUN_TEST(test_vm_translate_4kb_page);
    RUN_TEST(test_vm_translate_page_offset);
    RUN_TEST(test_vm_translate_entry_flags);
    RUN_TEST(test_vm_translate_nonzero_indexes);
    RUN_TEST(test_vm_translate_higher_half_address);
    RUN_TEST(test_vm_translate_missing_pdpt_entry);
    RUN_TEST(test_vm_translate_missing_pd_entry);
    RUN_TEST(test_vm_translate_missing_pt_entry);
    RUN_TEST(test_vm_translate_2mb_page);
    RUN_TEST(test_vm_translate_1gb_page);
    RUN_TEST(test_vm_translate_2mb_nonzero_base);
    RUN_TEST(test_vm_translate_1gb_nonzero_base);
    RUN_TEST(test_vm_translate_noncanonical_address);
    RUN_TEST(test_vm_translate_noncanonical_lower_half);
    RUN_TEST(test_vm_translate_5level_missing_pd_entry);
    RUN_TEST(test_vm_translate_5level_4kb_page);
    RUN_TEST(test_vm_translate_5level_extended_address);
    RUN_TEST(test_vm_translate_5level_higher_half);
    RUN_TEST(test_vm_translate_5level_noncanonical_address);
    RUN_TEST(test_vm_translate_missing_pml5_entry);
    RUN_TEST(test_vm_translate_5level_2mb_page);
    RUN_TEST(test_vm_translate_5level_1gb_page);
    RUN_TEST(test_vm_translate_physical_address_zero);

    return UNITY_END();
}