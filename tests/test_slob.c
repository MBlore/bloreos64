
#include "unity.h"

#include <slob.h>
#include <mem.h>

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define TEST_ALIGNMENT       16
#define TEST_MAX_ALLOCATIONS 256
#define TEST_MAX_PAGES       64
#define TEST_SMALL_COUNT     100
#define TEST_PATTERN_SIZE    256

typedef struct
{
    void *address;
    size_t pages;
} mock_allocation_t;

static mock_allocation_t mock_allocations[TEST_MAX_ALLOCATIONS];
static size_t mock_allocation_count;
static size_t mock_pages_allocated;
static size_t mock_pages_freed;
static size_t mock_allocation_calls;
static size_t mock_free_calls;
static size_t mock_invalid_frees;
static size_t mock_fail_after;

/*
 * Mock the physical memory manager with page-aligned host memory.
 * Track allocation sizes so multi-page frees can be verified.
 */
void *kpalloc(size_t num_pages)
{
    if (num_pages == 0 || num_pages > TEST_MAX_PAGES) {
        return NULL;
    }

    if (mock_allocation_calls >= mock_fail_after) {
        return NULL;
    }

    if (mock_allocation_count >= TEST_MAX_ALLOCATIONS) {
        return NULL;
    }

    void *memory = aligned_alloc(PAGE_SIZE, num_pages * PAGE_SIZE);

    if (memory == NULL) {
        return NULL;
    }

    mock_allocations[mock_allocation_count].address = memory;
    mock_allocations[mock_allocation_count].pages = num_pages;
    mock_allocation_count++;

    mock_allocation_calls++;
    mock_pages_allocated += num_pages;

    return memory;
}

/*
 * Verify that SLOB returns only original PMM allocation addresses.
 */
void kfree(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    for (size_t i = 0; i < mock_allocation_count; i++) {
        if (mock_allocations[i].address == ptr) {
            mock_pages_freed += mock_allocations[i].pages;
            mock_free_calls++;

            free(ptr);

            mock_allocations[i] = mock_allocations[mock_allocation_count - 1];
            mock_allocation_count--;
            return;
        }
    }

    mock_invalid_frees++;
}

/*
 * Release any remaining host allocations between tests.
 */
static void mock_reset(void)
{
    for (size_t i = 0; i < mock_allocation_count; i++) {
        free(mock_allocations[i].address);
    }

    memset(mock_allocations, 0, sizeof(mock_allocations));

    mock_allocation_count = 0;
    mock_pages_allocated = 0;
    mock_pages_freed = 0;
    mock_allocation_calls = 0;
    mock_free_calls = 0;
    mock_invalid_frees = 0;
    mock_fail_after = SIZE_MAX;
}

void setUp(void)
{
    mock_reset();
    slob_init();
}

void tearDown(void)
{
    TEST_ASSERT_EQUAL_UINT(0, mock_invalid_frees);
    TEST_ASSERT_EQUAL_UINT(0, mock_allocation_count);
}

/*
 * Check NULL handling and reject zero-byte allocations.
 */
void test_slob_null_and_zero(void)
{
    slob_free(NULL);

    TEST_ASSERT_NULL(slob_malloc(0));
    TEST_ASSERT_EQUAL_UINT(0, mock_allocation_calls);
}

/*
 * Reject requests that overflow size calculations.
 */
void test_slob_size_overflow(void)
{
    TEST_ASSERT_NULL(slob_malloc(SIZE_MAX));
    TEST_ASSERT_NULL(slob_malloc(SIZE_MAX - 7));

    TEST_ASSERT_EQUAL_UINT(0, mock_allocation_calls);
}

/*
 * Check 16-byte alignment across many allocation sizes.
 */
void test_slob_alignment(void)
{
    const size_t sizes[] = {
        1, 2, 3, 7, 8, 15, 16, 17,
        31, 32, 33, 63, 64, 65,
        127, 128, 255, 256, 511, 512,
        1023, 2048, 3000, 4000, 4096, 8192
    };

    const size_t count = sizeof(sizes) / sizeof(sizes[0]);
    void *allocations[sizeof(sizes) / sizeof(sizes[0])];

    for (size_t i = 0; i < count; i++) {
        allocations[i] = slob_malloc(sizes[i]);

        TEST_ASSERT_NOT_NULL(allocations[i]);
        TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t)allocations[i] % TEST_ALIGNMENT);
    }

    for (size_t i = 0; i < count; i++) {
        slob_free(allocations[i]);
    }
}

/*
 * Verify that a freed block can be reused.
 */
void test_slob_reuses_freed_block(void)
{
    void *a = slob_malloc(128);
    void *b = slob_malloc(128);
    void *c = slob_malloc(128);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);

    size_t previous_calls = mock_allocation_calls;

    slob_free(b);

    void *replacement = slob_malloc(128);

    TEST_ASSERT_EQUAL_PTR(b, replacement);
    TEST_ASSERT_EQUAL_UINT(previous_calls, mock_allocation_calls);

    slob_free(a);
    slob_free(c);
    slob_free(replacement);
}

/*
 * Verify that splitting a larger free block creates
 * two independently usable allocations.
 */
void test_slob_splits_free_blocks(void)
{
    void *a = slob_malloc(512);
    void *guard = slob_malloc(64);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(guard);

    slob_free(a);

    size_t previous_calls = mock_allocation_calls;

    void *b = slob_malloc(128);
    void *c = slob_malloc(128);

    TEST_ASSERT_EQUAL_PTR(a, b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_EQUAL(b, c);
    TEST_ASSERT_EQUAL_UINT(previous_calls, mock_allocation_calls);

    slob_free(b);
    slob_free(c);
    slob_free(guard);
}

/*
 * Verify that adjacent free blocks merge into a
 * larger allocation without requesting another page.
 */
void test_slob_coalesces_adjacent_blocks(void)
{
    void *a = slob_malloc(128);
    void *b = slob_malloc(128);
    void *guard = slob_malloc(128);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(guard);

    slob_free(a);
    slob_free(b);

    size_t previous_calls = mock_allocation_calls;

    void *merged = slob_malloc(240);

    TEST_ASSERT_EQUAL_PTR(a, merged);
    TEST_ASSERT_EQUAL_UINT(previous_calls, mock_allocation_calls);

    slob_free(merged);
    slob_free(guard);
}

/*
 * Verify coalescing when blocks are freed in reverse order.
 */
void test_slob_coalesces_reverse_order(void)
{
    void *a = slob_malloc(128);
    void *b = slob_malloc(128);
    void *c = slob_malloc(128);
    void *guard = slob_malloc(128);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(guard);

    slob_free(c);
    slob_free(b);
    slob_free(a);

    size_t previous_calls = mock_allocation_calls;

    void *merged = slob_malloc(400);

    TEST_ASSERT_EQUAL_PTR(a, merged);
    TEST_ASSERT_EQUAL_UINT(previous_calls, mock_allocation_calls);

    slob_free(merged);
    slob_free(guard);
}

/*
 * A partially occupied SLOB page must remain allocated.
 */
void test_slob_keeps_partially_used_page(void)
{
    void *a = slob_malloc(64);
    void *b = slob_malloc(128);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    slob_free(a);

    TEST_ASSERT_EQUAL_UINT(0, mock_pages_freed);

    slob_free(b);

    TEST_ASSERT_EQUAL_UINT(mock_pages_allocated, mock_pages_freed);
}

/*
 * Return an empty page to the PMM.
 */
void test_slob_reclaims_empty_page(void)
{
    void *a = slob_malloc(64);
    void *b = slob_malloc(128);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_UINT(1, mock_pages_allocated);

    slob_free(a);
    slob_free(b);

    TEST_ASSERT_EQUAL_UINT(1, mock_pages_freed);
    TEST_ASSERT_EQUAL_UINT(1, mock_free_calls);
}

/*
 * Allocate enough small objects to require multiple pages.
 */
void test_slob_multiple_pages(void)
{
    void *allocations[TEST_SMALL_COUNT];

    for (size_t i = 0; i < TEST_SMALL_COUNT; i++) {
        allocations[i] = slob_malloc(128);
        TEST_ASSERT_NOT_NULL(allocations[i]);
    }

    TEST_ASSERT_GREATER_THAN_UINT(1, mock_pages_allocated);

    for (size_t i = 0; i < TEST_SMALL_COUNT; i++) {
        slob_free(allocations[i]);
    }

    TEST_ASSERT_EQUAL_UINT(mock_pages_allocated, mock_pages_freed);
}

/*
 * Large allocations must obtain contiguous PMM pages.
 */
void test_slob_large_allocation(void)
{
    void *ptr = slob_malloc(9000);

    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_UINT(3, mock_pages_allocated);

    memset(ptr, 0x5A, 9000);

    for (size_t i = 0; i < 9000; i++) {
        TEST_ASSERT_EQUAL_HEX8(0x5A, ((uint8_t *)ptr)[i]);
    }

    slob_free(ptr);

    TEST_ASSERT_EQUAL_UINT(3, mock_pages_freed);
    TEST_ASSERT_EQUAL_UINT(1, mock_free_calls);
}

/*
 * Free large allocations in a different order.
 */
void test_slob_multiple_large_allocations(void)
{
    void *a = slob_malloc(5000);
    void *b = slob_malloc(9000);
    void *c = slob_malloc(16000);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);

    memset(a, 0x11, 5000);
    memset(b, 0x22, 9000);
    memset(c, 0x33, 16000);

    slob_free(b);
    slob_free(a);
    slob_free(c);

    TEST_ASSERT_EQUAL_UINT(mock_pages_allocated, mock_pages_freed);
    TEST_ASSERT_EQUAL_UINT(3, mock_free_calls);
}

/*
 * Ensure that neighbouring allocations do not overwrite
 * each other's contents.
 */
void test_slob_memory_integrity(void)
{
    uint8_t *a = slob_malloc(TEST_PATTERN_SIZE);
    uint8_t *b = slob_malloc(TEST_PATTERN_SIZE);
    uint8_t *c = slob_malloc(TEST_PATTERN_SIZE);

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_NULL(c);

    memset(a, 0xAA, TEST_PATTERN_SIZE);
    memset(b, 0xBB, TEST_PATTERN_SIZE);
    memset(c, 0xCC, TEST_PATTERN_SIZE);

    for (size_t i = 0; i < TEST_PATTERN_SIZE; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAA, a[i]);
        TEST_ASSERT_EQUAL_HEX8(0xBB, b[i]);
        TEST_ASSERT_EQUAL_HEX8(0xCC, c[i]);
    }

    slob_free(b);

    for (size_t i = 0; i < TEST_PATTERN_SIZE; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAA, a[i]);
        TEST_ASSERT_EQUAL_HEX8(0xCC, c[i]);
    }

    slob_free(a);
    slob_free(c);
}

/*
 * Allocation failure must return NULL without corrupting
 * existing allocations.
 */
void test_slob_out_of_memory(void)
{
    uint8_t *a = slob_malloc(3900);

    TEST_ASSERT_NOT_NULL(a);

    memset(a, 0xAB, 3900);

    mock_fail_after = mock_allocation_calls;

    void *b = slob_malloc(3900);

    TEST_ASSERT_NULL(b);

    for (size_t i = 0; i < 3900; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xAB, a[i]);
    }

    slob_free(a);
}

/*
 * The allocator must recover after PMM allocation failure.
 */
void test_slob_recovers_after_oom(void)
{
    mock_fail_after = 0;

    TEST_ASSERT_NULL(slob_malloc(128));

    mock_fail_after = SIZE_MAX;

    void *ptr = slob_malloc(128);

    TEST_ASSERT_NOT_NULL(ptr);

    slob_free(ptr);
}

/*
 * Test allocations around the single-page capacity boundary.
 */
void test_slob_page_boundary(void)
{
    void *small = slob_malloc(4000);

    TEST_ASSERT_NOT_NULL(small);
    TEST_ASSERT_EQUAL_UINT(1, mock_pages_allocated);

    slob_free(small);

    void *large = slob_malloc(PAGE_SIZE);

    TEST_ASSERT_NOT_NULL(large);
    TEST_ASSERT_GREATER_THAN_UINT(1, mock_pages_allocated);

    slob_free(large);

    TEST_ASSERT_EQUAL_UINT(mock_pages_allocated, mock_pages_freed);
}

/*
 * Verify that allocating and releasing many objects
 * does not leak PMM pages.
 */
void test_slob_repeated_allocations(void)
{
    for (size_t round = 0; round < 20; round++) {
        void *allocations[32];

        for (size_t i = 0; i < 32; i++) {
            allocations[i] = slob_malloc(32 + i * 8);
            TEST_ASSERT_NOT_NULL(allocations[i]);
        }

        for (size_t i = 0; i < 32; i += 2) {
            slob_free(allocations[i]);
        }

        for (size_t i = 1; i < 32; i += 2) {
            slob_free(allocations[i]);
        }
    }

    TEST_ASSERT_EQUAL_UINT(mock_pages_allocated, mock_pages_freed);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_slob_null_and_zero);
    RUN_TEST(test_slob_size_overflow);
    RUN_TEST(test_slob_alignment);
    RUN_TEST(test_slob_reuses_freed_block);
    RUN_TEST(test_slob_splits_free_blocks);
    RUN_TEST(test_slob_coalesces_adjacent_blocks);
    RUN_TEST(test_slob_coalesces_reverse_order);
    RUN_TEST(test_slob_keeps_partially_used_page);
    RUN_TEST(test_slob_reclaims_empty_page);
    RUN_TEST(test_slob_multiple_pages);
    RUN_TEST(test_slob_large_allocation);
    RUN_TEST(test_slob_multiple_large_allocations);
    RUN_TEST(test_slob_memory_integrity);
    RUN_TEST(test_slob_out_of_memory);
    RUN_TEST(test_slob_recovers_after_oom);
    RUN_TEST(test_slob_page_boundary);
    RUN_TEST(test_slob_repeated_allocations);

    return UNITY_END();
}
