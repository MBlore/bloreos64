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
#define TEST_LARGE_PAGE_PAT (1ULL << 12)

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

static void setup_5level_paging(void)
{
    // Simulate a CPU operating in five-level paging mode.
    mock_cr4 = CR4_PAE | CR4_LA57;
    mock_cr3 = TEST_PML5_PHYSICAL_ADDRESS;
    mock_address_widths = TEST_PHYSICAL_ADDRESS_WIDTH |
                          (TEST_LINEAR_ADDRESS_WIDTH_5LEVEL << 8);
    vm_init();
}

static void link_4level_page_tables(void)
{
    // Connect the four levels of the paging hierarchy.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pd[0] = TEST_PT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
}

static void link_5level_page_tables(void)
{
    pml5[0] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    link_4level_page_tables();
}

static void assert_translation_failure(uint64_t virtual_address)
{
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_FALSE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX, result);
}

static void assert_translation(uint64_t virtual_address,
                               uint64_t expected_physical_address)
{
    uint64_t result = UINT64_MAX;
    TEST_ASSERT_TRUE(vm_translate(virtual_address, &result));
    TEST_ASSERT_EQUAL_UINT64(expected_physical_address, result);
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
    assert_translation_failure(virtual_address);
}

void test_vm_translate_4kb_page(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Link the four levels of the paging hierarchy.
    link_4level_page_tables();

    // Map the virtual page to a physical page.
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    assert_translation(virtual_address, physical_address);
}

void test_vm_translate_page_offset(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;
    const uint64_t offset = 0x234;

    // Link the four levels of the paging hierarchy.
    link_4level_page_tables();

    pt[1] = physical_address | TEST_PAGE_PRESENT;

    // Translation must preserve the offset within the page.
    assert_translation(virtual_address + offset, physical_address + offset);
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
    assert_translation(virtual_address, physical_address);
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

    assert_translation(virtual_address, physical_address);
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

    assert_translation(virtual_address, physical_address);
}

void test_vm_translate_missing_pdpt_entry(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // Link the PML4 to the PDPT.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // The PDPT entry remains empty.

    // Translation must stop at the missing entry.
    assert_translation_failure(virtual_address);
}

void test_vm_translate_missing_pd_entry(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // Link the PML4 and PDPT tables.
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // The PD entry remains empty.

    assert_translation_failure(virtual_address);
}

void test_vm_translate_missing_pt_entry(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;

    // Link all four levels of the paging hierarchy.
    link_4level_page_tables();

    // The final PT entry remains empty.

    assert_translation_failure(virtual_address);
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

    assert_translation(virtual_address + offset, physical_address + offset);
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

    assert_translation(virtual_address + offset, physical_address + offset);
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

    assert_translation(virtual_address + offset, physical_address + offset);
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

    assert_translation(virtual_address + offset, physical_address + offset);
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
    assert_translation_failure(virtual_address);
}

void test_vm_translate_noncanonical_lower_half(void)
{
    vm_init();

    const uint64_t virtual_address = 0xFFFF000000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Create a valid mapping in the lower half.
    link_4level_page_tables();
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    // Reject incorrect sign extension of bit 47.
    assert_translation_failure(virtual_address);
}

void test_vm_translate_5level_missing_pd_entry(void)
{
    setup_5level_paging();

    const uint64_t virtual_address = PAGE_SIZE;

    // Link the first three levels, leaving the PD entry empty.
    pml5[0] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    assert_translation_failure(virtual_address);
}

void test_vm_translate_5level_4kb_page(void)
{
    // Simulate active five-level paging.
    setup_5level_paging();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Link all five levels of the paging hierarchy.
    link_5level_page_tables();
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    assert_translation(virtual_address, physical_address);
}

void test_vm_translate_5level_extended_address(void)
{
    setup_5level_paging();

    const uint64_t virtual_address = 0x0001000000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Use a non-zero PML5 index.
    pml5[1] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    link_4level_page_tables();
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    assert_translation(virtual_address, physical_address);
}

void test_vm_translate_5level_higher_half(void)
{
    setup_5level_paging();

    const uint64_t virtual_address = 0xFF00000000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Map an address in the five-level higher half.
    pml5[256] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    link_4level_page_tables();
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    assert_translation(virtual_address, physical_address);
}

void test_vm_translate_5level_noncanonical_address(void)
{
    setup_5level_paging();

    const uint64_t virtual_address = 0x0100000000001000ULL;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    // Create a valid mapping in the higher half.
    pml5[256] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    link_4level_page_tables();
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    // Reject an incorrectly sign-extended virtual address.
    assert_translation_failure(virtual_address);
}

void test_vm_translate_missing_pml5_entry(void)
{
    setup_5level_paging();

    const uint64_t virtual_address = PAGE_SIZE;

    // Leave the PML5 entry empty.
    // The walker must terminate at this level.
    assert_translation_failure(virtual_address);
}

void test_vm_translate_5level_2mb_page(void)
{
    setup_5level_paging();

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

    assert_translation(virtual_address + offset, physical_address + offset);
}

void test_vm_translate_5level_1gb_page(void)
{
    setup_5level_paging();

    const uint64_t virtual_address = TEST_HUGE_PAGE_SIZE;
    const uint64_t physical_address = 4 * TEST_HUGE_PAGE_SIZE;
    const uint64_t offset = 0x12345678;

    pml5[0] = TEST_PML4_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Terminate the walk at the PDPT.
    pdpt[1] = physical_address |
              TEST_PAGE_PRESENT |
              TEST_PAGE_SIZE_FLAG;

    assert_translation(virtual_address + offset, physical_address + offset);
}

void test_vm_translate_physical_address_zero(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 0;

    link_4level_page_tables();

    // Physical address zero is a valid mapping.
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    assert_translation(virtual_address, physical_address);
}

void test_vm_translate_null_output(void)
{
    vm_init();

    const uint64_t virtual_address = PAGE_SIZE;
    const uint64_t physical_address = 8 * PAGE_SIZE;

    link_4level_page_tables();
    pt[1] = physical_address | TEST_PAGE_PRESENT;

    // Reject a NULL output pointer, even for a valid mapping.
    TEST_ASSERT_FALSE(vm_translate(virtual_address, NULL));
}

void test_vm_translate_2mb_pat_flag(void)
{
    vm_init();

    const uint64_t virtual_address = TEST_LARGE_PAGE_SIZE;
    const uint64_t physical_address = 4 * TEST_LARGE_PAGE_SIZE;

    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;
    pdpt[0] = TEST_PD_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Map a 2 MiB page with the PAT flag set.
    pd[1] = physical_address |
            TEST_PAGE_PRESENT |
            TEST_PAGE_SIZE_FLAG |
            TEST_LARGE_PAGE_PAT;

    // The PAT flag must not affect the physical address.
    assert_translation(virtual_address, physical_address);
}

void test_vm_translate_1gb_pat_flag(void)
{
    vm_init();

    const uint64_t virtual_address = TEST_HUGE_PAGE_SIZE;
    const uint64_t physical_address = 4 * TEST_HUGE_PAGE_SIZE;

    pml4[0] = TEST_PDPT_PHYSICAL_ADDRESS | TEST_PAGE_PRESENT;

    // Map a 1 GiB page with the PAT flag set.
    pdpt[1] = physical_address |
              TEST_PAGE_PRESENT |
              TEST_PAGE_SIZE_FLAG |
              TEST_LARGE_PAGE_PAT;

    // The PAT flag must not affect the physical address.
    assert_translation(virtual_address, physical_address);
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
    RUN_TEST(test_vm_translate_null_output);
    RUN_TEST(test_vm_translate_2mb_pat_flag);
    RUN_TEST(test_vm_translate_1gb_pat_flag);

    return UNITY_END();
}