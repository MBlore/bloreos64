#ifndef VM_TEST_HELPERS_H
#define VM_TEST_HELPERS_H

#include <stdint.h>
#include <mem.h>

#define PAGE_TABLE_ENTRIES 512

#define TEST_PHYSICAL_ADDRESS_WIDTH 40
#define TEST_LINEAR_ADDRESS_WIDTH 48
#define TEST_LINEAR_ADDRESS_WIDTH_5LEVEL 57ULL

#define TEST_PML4_PHYSICAL_ADDRESS PAGE_SIZE
#define TEST_PDPT_PHYSICAL_ADDRESS (2 * PAGE_SIZE)
#define TEST_PD_PHYSICAL_ADDRESS   (3 * PAGE_SIZE)
#define TEST_PT_PHYSICAL_ADDRESS   (4 * PAGE_SIZE)
#define TEST_PML5_PHYSICAL_ADDRESS (5 * PAGE_SIZE)

#define TEST_PAGE_PRESENT   0x1ULL
#define TEST_PAGE_RW        0x2ULL
#define TEST_PAGE_USER      0x4ULL
#define TEST_PAGE_SIZE_FLAG (1ULL << 7)
#define TEST_LARGE_PAGE_PAT (1ULL << 12)

#define TEST_LARGE_PAGE_SIZE (2ULL * 1024 * 1024)
#define TEST_HUGE_PAGE_SIZE  (1ULL * 1024 * 1024 * 1024)

// Pages reserved for existing paging structures.
#define TEST_RESERVED_PAGE_COUNT 5

// Additional pages available to the mock physical allocator.
#define TEST_ALLOCATABLE_PAGE_COUNT 8

// Total simulated physical memory in pages.
#define TEST_PAGE_TABLE_COUNT \
    (TEST_RESERVED_PAGE_COUNT + TEST_ALLOCATABLE_PAGE_COUNT)

// Simulated physical memory containing the page tables.
extern uint64_t page_tables[TEST_PAGE_TABLE_COUNT][PAGE_TABLE_ENTRIES];

// Individual paging structures.
#define pml4 page_tables[0]
#define pdpt page_tables[1]
#define pd   page_tables[2]
#define pt   page_tables[3]
#define pml5 page_tables[4]

// Paging configuration.
void setup_5level_paging(void);

// Page table hierarchy construction.
void link_4level_page_tables(void);
void link_5level_page_tables(void);

// Virtual address translation assertions.
void assert_translation_failure(uint64_t virtual_address);

void assert_translation(uint64_t virtual_address,
                        uint64_t expected_physical_address);

#endif