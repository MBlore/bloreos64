#include "unity.h"
#include <stdint.h>
#include <mem.h>

#define TEST_PAGE_COUNT 16
#define TEST_MEMORY_SIZE (TEST_PAGE_COUNT * PAGE_SIZE)

// Simulated physical RAM, aligned to a page boundary.
_Alignas(PAGE_SIZE)
static uint8_t physical_memory[TEST_MEMORY_SIZE];

void setUp(void)
{
}

void tearDown(void)
{
}

void test_physical_memory_alignment(void)
{
    uintptr_t address = (uintptr_t)physical_memory;

    TEST_ASSERT_EQUAL_UINT64(0, address % PAGE_SIZE);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_physical_memory_alignment);

    return UNITY_END();
}