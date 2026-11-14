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

// Bootloader requests defined in mem.c.
extern volatile struct limine_memmap_request memmap_request;
extern volatile struct limine_hhdm_request hhdm_request;

// Simulated bootloader HHDM response.
static struct limine_hhdm_response hhdm_response;

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

    // Simulate the responses normally provided by Limine.
    hhdm_response.revision = 0;
    hhdm_response.offset = vmm_higher_half_offset;

    memmap_request.response = &memory_map;
    hhdm_request.response = &hhdm_response;
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

void test_pmm_initialisation(void)
{
    const size_t metadata_pages = 2;
    const size_t metadata_bytes = metadata_pages * PAGE_SIZE;
    const size_t expected_free_pages = TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    TEST_ASSERT_EQUAL_UINT64(
        TEST_MEMORY_SIZE,
        total_memory_bytes
    );

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        max_pages_available
    );

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );

    TEST_ASSERT_EQUAL_UINT64(
        TEST_PHYSICAL_BASE + metadata_bytes,
        usable_region.base
    );

    TEST_ASSERT_EQUAL_UINT64(
        TEST_MEMORY_SIZE - metadata_bytes,
        usable_region.length
    );
}

void test_pmm_alloc_single_page(void)
{
    const size_t metadata_pages = 2;

    kmem_init();

    void *ptr = kalloc(PAGE_SIZE);

    // Ensure the allocation does not overlap PMM metadata.
    TEST_ASSERT_EQUAL_PTR(
        physical_memory + (metadata_pages * PAGE_SIZE),
        ptr
    );

    // The number of free pages should decrease.
    TEST_ASSERT_EQUAL_UINT64(
        TEST_PAGE_COUNT - metadata_pages - 1,
        num_pages_available
    );

    // The total runtime allocation capacity remains unchanged.
    TEST_ASSERT_EQUAL_UINT64(
        TEST_PAGE_COUNT - metadata_pages,
        max_pages_available
    );
}

void test_pmm_alloc_contiguous(void)
{
    const size_t metadata_pages = 2;
    const size_t first_pages = 3;
    const size_t second_pages = 1;

    kmem_init();

    void *first = kalloc(first_pages * PAGE_SIZE);
    void *second = kalloc(second_pages * PAGE_SIZE);

    // The first allocation must start after the PMM metadata.
    TEST_ASSERT_EQUAL_PTR(
        physical_memory + (metadata_pages * PAGE_SIZE),
        first
    );

    // The second allocation must not overlap the first.
    TEST_ASSERT_EQUAL_PTR(
        physical_memory + ((metadata_pages + first_pages) * PAGE_SIZE),
        second
    );

    // Verify the remaining number of free physical pages.
    TEST_ASSERT_EQUAL_UINT64(
        TEST_PAGE_COUNT - metadata_pages - first_pages - second_pages,
        num_pages_available
    );
}

void test_pmm_alloc_rounding(void)
{
    const size_t metadata_pages = 2;
    const size_t requested_bytes = PAGE_SIZE + 1;
    const size_t expected_pages = 2;

    kmem_init();

    void *ptr = kalloc(requested_bytes);

    TEST_ASSERT_EQUAL_PTR(
        physical_memory + (metadata_pages * PAGE_SIZE),
        ptr
    );

    // Verify that two physical pages have been reserved.
    TEST_ASSERT_EQUAL_UINT64(
        TEST_PAGE_COUNT - metadata_pages - expected_pages,
        num_pages_available
    );
}

void test_pmm_alloc_exhausted(void)
{
    const size_t metadata_pages = 2;
    const size_t free_pages = TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    // Allocate all remaining physical memory.
    void *ptr = kalloc(free_pages * PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(ptr);

    TEST_ASSERT_EQUAL_UINT64(0, num_pages_available);

    // Further allocations must fail.
    void *extra = kalloc(PAGE_SIZE);

    TEST_ASSERT_NULL(extra);

    // A failed allocation must not change the free page count.
    TEST_ASSERT_EQUAL_UINT64(0, num_pages_available);
}

void test_pmm_alloc_skips_reserved(void)
{
    const size_t metadata_pages = 2;
    const size_t first_region_pages = 6;
    const size_t reserved_pages = 4;
    const size_t second_region_start =
        first_region_pages + reserved_pages;

    usable_region.length = first_region_pages * PAGE_SIZE;

    struct limine_memmap_entry reserved_region = {
        .base = TEST_PHYSICAL_BASE + first_region_pages * PAGE_SIZE,
        .length = reserved_pages * PAGE_SIZE,
        .type = LIMINE_MEMMAP_RESERVED
    };

    struct limine_memmap_entry second_region = {
        .base = TEST_PHYSICAL_BASE + second_region_start * PAGE_SIZE,
        .length = (TEST_PAGE_COUNT - second_region_start) * PAGE_SIZE,
        .type = LIMINE_MEMMAP_USABLE
    };

    struct limine_memmap_entry *entries[] = {
        &usable_region,
        &reserved_region,
        &second_region
    };

    memory_map.entries = entries;
    memory_map.entry_count = sizeof(entries) / sizeof(entries[0]);

    kmem_init();

    // Exhaust the first usable region.
    void *first = kalloc(
        (first_region_pages - metadata_pages) * PAGE_SIZE
    );

    TEST_ASSERT_EQUAL_PTR(
        physical_memory + metadata_pages * PAGE_SIZE,
        first
    );

    // The next allocation must skip the reserved region.
    void *second = kalloc(PAGE_SIZE);

    TEST_ASSERT_EQUAL_PTR(
        physical_memory + second_region_start * PAGE_SIZE,
        second
    );
}

void test_pmm_free_single_page(void)
{
    const size_t metadata_pages = 2;
    const size_t expected_free_pages =
        TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    void *ptr = kalloc(PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(ptr);

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages - 1,
        num_pages_available
    );

    kfree(ptr);

    // Freeing a page must restore the free page counter.
    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_free_multiple_pages(void)
{
    const size_t metadata_pages = 2;
    const size_t allocated_pages = 4;
    const size_t expected_free_pages =
        TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    void *ptr = kalloc(allocated_pages * PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(ptr);

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages - allocated_pages,
        num_pages_available
    );

    kfree(ptr);

    // All pages belonging to the allocation must be released.
    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_reuse_freed_pages(void)
{
    const size_t metadata_pages = 2;
    const size_t free_pages = TEST_PAGE_COUNT - metadata_pages;
    const size_t allocation_size = free_pages * PAGE_SIZE;

    kmem_init();

    // Allocate all available physical pages.
    void *first = kalloc(allocation_size);

    TEST_ASSERT_NOT_NULL(first);

    // Release all allocated pages.
    kfree(first);

    // Allocate the same physical pages again.
    void *second = kalloc(allocation_size);

    TEST_ASSERT_EQUAL_PTR(first, second);
    TEST_ASSERT_EQUAL_UINT64(0, num_pages_available);
}

void test_pmm_free_last_page(void)
{
    const size_t metadata_pages = 2;
    const size_t free_pages = TEST_PAGE_COUNT - metadata_pages;
    const size_t last_page_index = TEST_PAGE_COUNT - 1;

    kmem_init();

    // Allocate all available pages except the last one.
    void *first = kalloc((free_pages - 1) * PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(first);

    // Allocate the final physical page.
    void *last = kalloc(PAGE_SIZE);

    TEST_ASSERT_EQUAL_PTR(
        physical_memory + last_page_index * PAGE_SIZE,
        last
    );

    // Release the final physical page.
    kfree(last);

    TEST_ASSERT_EQUAL_UINT64(1, num_pages_available);
}

void test_pmm_alloc_fragmented(void)
{
    const size_t block_pages = 4;
    const size_t requested_pages = 7;
    const size_t metadata_pages = 2;

    kmem_init();

    void *first = kalloc(block_pages * PAGE_SIZE);
    void *second = kalloc(block_pages * PAGE_SIZE);
    void *third = kalloc(block_pages * PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_NOT_NULL(third);

    // Create two separate regions of free physical memory.
    kfree(first);
    kfree(third);

    const size_t expected_free_pages =
        TEST_PAGE_COUNT - metadata_pages - block_pages;

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );

    // No contiguous region is large enough for this allocation.
    void *ptr = kalloc(requested_pages * PAGE_SIZE);

    TEST_ASSERT_NULL(ptr);

    // A failed allocation must not consume any physical pages.
    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_alloc_zero_bytes(void)
{
    const size_t metadata_pages = 2;
    const size_t expected_free_pages =
        TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    // A zero-byte allocation must not reserve physical memory.
    void *ptr = kalloc(0);

    TEST_ASSERT_NULL(ptr);

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_alloc_overflow(void)
{
    const size_t metadata_pages = 2;
    const size_t expected_free_pages =
        TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    // An excessively large allocation must fail safely.
    void *ptr = kalloc(SIZE_MAX);

    TEST_ASSERT_NULL(ptr);

    // A failed allocation must not consume physical memory.
    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_kpalloc_overflow(void)
{
    const size_t metadata_pages = 2;
    const size_t expected_free_pages = TEST_PAGE_COUNT - metadata_pages;
    const size_t requested_pages = (SIZE_MAX / PAGE_SIZE) + 2;

    kmem_init();

    void *ptr = kpalloc(requested_pages);

    TEST_ASSERT_NULL(ptr);

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_free_null(void)
{
    const size_t metadata_pages = 2;
    const size_t expected_free_pages = TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    // Freeing a NULL pointer must be a safe no-op.
    kfree(NULL);

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_free_unaligned(void)
{
    const size_t metadata_pages = 2;
    const size_t unaligned_offset = 128;
    const size_t expected_free_pages =
        TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    void *ptr = kalloc(PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(ptr);

    // Attempt to free an address inside the allocated page.
    kfree((uint8_t *)ptr + unaligned_offset);

    // The original allocation must remain reserved.
    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages - 1,
        num_pages_available
    );

    // Free the allocation using its correct address.
    kfree(ptr);

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_free_out_of_bounds(void)
{
    const size_t metadata_pages = 2;
    const size_t expected_free_pages =
        TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    // Construct an address immediately beyond managed memory.
    void *ptr = physical_memory + TEST_MEMORY_SIZE;

    kfree(ptr);

    // Rejecting an invalid pointer must not change memory accounting.
    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_double_free(void)
{
    const size_t metadata_pages = 2;
    const size_t expected_free_pages =
        TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    void *ptr = kalloc(PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(ptr);

    kfree(ptr);

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );

    // Attempt to release the same allocation again.
    kfree(ptr);

    // The second deallocation must not change the free page count.
    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
}

void test_pmm_free_interior_page(void)
{
    const size_t metadata_pages = 2;
    const size_t allocated_pages = 4;
    const size_t expected_free_pages =
        TEST_PAGE_COUNT - metadata_pages;

    kmem_init();

    void *ptr = kalloc(allocated_pages * PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(ptr);

    // Attempt to free the second page of the allocation.
    kfree((uint8_t *)ptr + PAGE_SIZE);

    // The entire allocation must remain reserved.
    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages - allocated_pages,
        num_pages_available
    );

    // Release the allocation using its starting address.
    kfree(ptr);

    TEST_ASSERT_EQUAL_UINT64(
        expected_free_pages,
        num_pages_available
    );
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

    RUN_TEST(test_pmm_initialisation);
    RUN_TEST(test_pmm_alloc_single_page);
    RUN_TEST(test_pmm_alloc_contiguous);
    RUN_TEST(test_pmm_alloc_rounding);
    RUN_TEST(test_pmm_alloc_exhausted);
    RUN_TEST(test_pmm_alloc_skips_reserved);
    RUN_TEST(test_pmm_free_single_page);
    RUN_TEST(test_pmm_free_multiple_pages);
    RUN_TEST(test_pmm_reuse_freed_pages);
    RUN_TEST(test_pmm_free_last_page);
    RUN_TEST(test_pmm_alloc_fragmented);
    RUN_TEST(test_pmm_alloc_zero_bytes);
    RUN_TEST(test_pmm_alloc_overflow);
    RUN_TEST(test_pmm_kpalloc_overflow);
    RUN_TEST(test_pmm_free_null);
    RUN_TEST(test_pmm_free_unaligned);
    RUN_TEST(test_pmm_free_out_of_bounds);
    RUN_TEST(test_pmm_double_free);
    RUN_TEST(test_pmm_free_interior_page);

    return UNITY_END();
}