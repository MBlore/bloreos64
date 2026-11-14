#include "unity.h"
#include <stdint.h>
#include <mem.h>

#define TEST_PHYSICAL_BASE PAGE_SIZE
#define TEST_PAGE_COUNT 16
#define TEST_MEMORY_SIZE (TEST_PAGE_COUNT * PAGE_SIZE)

jmp_buf mock_hcf_context;
int mock_hcf_expected = 0;

// Simulated physical RAM, aligned to a page boundary.
_Alignas(PAGE_SIZE)
static uint8_t physical_memory[TEST_MEMORY_SIZE];

static struct limine_memmap_entry usable_region;
static struct limine_memmap_entry *memory_entries[] = {
    &usable_region
};
static struct limine_memmap_response memory_map;

void setUp(void)
{
    mock_hcf_expected = 0;

    usable_region.base = TEST_PHYSICAL_BASE;
    usable_region.length = TEST_MEMORY_SIZE;
    usable_region.type = LIMINE_MEMMAP_USABLE;

    memory_map.revision = 0;
    memory_map.entry_count = 1;
    memory_map.entries = memory_entries;

    memmap = &memory_map;
    vmm_higher_half_offset = (uintptr_t)physical_memory - TEST_PHYSICAL_BASE;
    max_pages_available = TEST_PAGE_COUNT;
}

void tearDown(void)
{
}

void test_physical_memory_alignment(void)
{
    uintptr_t address = (uintptr_t)physical_memory;

    TEST_ASSERT_EQUAL_UINT64(0, address % PAGE_SIZE);
}

void test_memmap_alloc_single_page(void)
{
    void *ptr = memmap_alloc(PAGE_SIZE);

    TEST_ASSERT_EQUAL_PTR(physical_memory, ptr);

    // Test the base address advanced by one page.
    TEST_ASSERT_EQUAL_UINT64(
        TEST_PHYSICAL_BASE + PAGE_SIZE,
        usable_region.base
    );

    // Test the memory region length decreases by one page.
    TEST_ASSERT_EQUAL_UINT64(
        TEST_MEMORY_SIZE - PAGE_SIZE,
        usable_region.length
    );

    // Tes that available page counter decreases by one.
    TEST_ASSERT_EQUAL_UINT64(
        TEST_PAGE_COUNT - 1,
        max_pages_available
    );
}

void test_memmap_alloc_rounding(void)
{
    // Test that requesting more bytes than one page rounds up to the next page size.

    const size_t requested_bytes = PAGE_SIZE + 1;
    const size_t expected_pages = 2;
    const size_t expected_bytes = expected_pages * PAGE_SIZE;

    void *ptr = memmap_alloc(requested_bytes);

    TEST_ASSERT_EQUAL_PTR(physical_memory, ptr);

    TEST_ASSERT_EQUAL_UINT64(
        TEST_PHYSICAL_BASE + expected_bytes,
        usable_region.base
    );

    TEST_ASSERT_EQUAL_UINT64(
        TEST_MEMORY_SIZE - expected_bytes,
        usable_region.length
    );

    TEST_ASSERT_EQUAL_UINT64(
        TEST_PAGE_COUNT - expected_pages,
        max_pages_available
    );
}

void test_memmap_alloc_consecutive(void)
{
    // Test that consecutive calls to memmap_alloc don't return overlapping regions of memory.
    const size_t first_pages = 1;
    const size_t second_pages = 2;
    const size_t total_pages = first_pages + second_pages;

    void *first = memmap_alloc(first_pages * PAGE_SIZE);
    void *second = memmap_alloc(second_pages * PAGE_SIZE);

    TEST_ASSERT_EQUAL_PTR(physical_memory, first);

    TEST_ASSERT_EQUAL_PTR(
        physical_memory + (first_pages * PAGE_SIZE),
        second
    );

    TEST_ASSERT_EQUAL_UINT64(
        TEST_PHYSICAL_BASE + (total_pages * PAGE_SIZE),
        usable_region.base
    );

    TEST_ASSERT_EQUAL_UINT64(
        TEST_MEMORY_SIZE - (total_pages * PAGE_SIZE),
        usable_region.length
    );

    TEST_ASSERT_EQUAL_UINT64(
        TEST_PAGE_COUNT - total_pages,
        max_pages_available
    );
}

void test_memmap_alloc_zeroed(void)
{
    const uint8_t dirty_value = 0xFF;

    // Simulate RAM containing existing data.
    memset(physical_memory, dirty_value, TEST_MEMORY_SIZE);

    uint8_t *ptr = memmap_alloc(PAGE_SIZE);

    TEST_ASSERT_EQUAL_PTR(physical_memory, ptr);

    // Verify that the allocated page has been zeroed.
    for (size_t i = 0; i < PAGE_SIZE; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x00, ptr[i]);
    }

    // Verify that unallocated memory remains untouched.
    for (size_t i = PAGE_SIZE; i < TEST_MEMORY_SIZE; i++) {
        TEST_ASSERT_EQUAL_HEX8(dirty_value, physical_memory[i]);
    }
}

void test_memmap_alloc_skips_reserved(void)
{
    const size_t reserved_pages = 4;
    const size_t reserved_bytes = reserved_pages * PAGE_SIZE;

    struct limine_memmap_entry reserved_region = {
        .base = TEST_PHYSICAL_BASE,
        .length = reserved_bytes,
        .type = LIMINE_MEMMAP_RESERVED
    };

    usable_region.base += reserved_bytes;
    usable_region.length -= reserved_bytes;

    struct limine_memmap_entry *entries[] = {
        &reserved_region,
        &usable_region
    };

    memory_map.entries = entries;
    memory_map.entry_count = 2;

    max_pages_available = TEST_PAGE_COUNT - reserved_pages;

    void *ptr = memmap_alloc(PAGE_SIZE);

    // Allocation must start after the reserved region.
    TEST_ASSERT_EQUAL_PTR(
        physical_memory + reserved_bytes,
        ptr
    );

    // The reserved region must remain unchanged.
    TEST_ASSERT_EQUAL_UINT64(
        TEST_PHYSICAL_BASE,
        reserved_region.base
    );

    TEST_ASSERT_EQUAL_UINT64(
        reserved_bytes,
        reserved_region.length
    );

    TEST_ASSERT_EQUAL_UINT64(
        TEST_PAGE_COUNT - reserved_pages - 1,
        max_pages_available
    );
}

void test_memmap_alloc_full_region(void)
{
    void *ptr = memmap_alloc(TEST_MEMORY_SIZE);

    TEST_ASSERT_EQUAL_PTR(physical_memory, ptr);

    TEST_ASSERT_EQUAL_UINT64(
        TEST_PHYSICAL_BASE + TEST_MEMORY_SIZE,
        usable_region.base
    );

    TEST_ASSERT_EQUAL_UINT64(0, usable_region.length);
    TEST_ASSERT_EQUAL_UINT64(0, max_pages_available);
}

void test_memmap_alloc_exhausted(void)
{
    // Exhaust all available physical memory.
    memmap_alloc(TEST_MEMORY_SIZE);

    mock_hcf_expected = 1;

    if (setjmp(mock_hcf_context) == 0) {
        // This allocation must trigger a kernel halt.
        memmap_alloc(PAGE_SIZE);

        TEST_FAIL_MESSAGE("Expected kernel halt");
    }

    mock_hcf_expected = 0;

    // The failed allocation must not modify the memory map.
    TEST_ASSERT_EQUAL_UINT64(
        TEST_PHYSICAL_BASE + TEST_MEMORY_SIZE,
        usable_region.base
    );

    TEST_ASSERT_EQUAL_UINT64(0, usable_region.length);
    TEST_ASSERT_EQUAL_UINT64(0, max_pages_available);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_physical_memory_alignment);
    RUN_TEST(test_memmap_alloc_single_page);
    RUN_TEST(test_memmap_alloc_rounding);
    RUN_TEST(test_memmap_alloc_consecutive);
    RUN_TEST(test_memmap_alloc_zeroed);
    RUN_TEST(test_memmap_alloc_skips_reserved);
    RUN_TEST(test_memmap_alloc_full_region);
    RUN_TEST(test_memmap_alloc_exhausted);
    
    return UNITY_END();
}