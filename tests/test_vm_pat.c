#include "unity.h"
#include "mocks/cpu.h"
#include <vm.h>

void test_vm_pat_null_output(void)
{
    // Reject an invalid output pointer.
    TEST_ASSERT_FALSE(
        vm_get_memory_type_flags(VM_MEMORY_UC, NULL)
    );
}

void test_vm_pat_find_uncacheable(void)
{
    uint64_t flags = UINT64_MAX;

    TEST_ASSERT_TRUE(
        vm_get_memory_type_flags(VM_MEMORY_UC, &flags)
    );

    TEST_ASSERT_EQUAL_HEX64(
        PAGE_PCD | PAGE_PWT,
        flags
    );
}

void test_vm_pat_find_uncacheable_custom(void)
{
    // UC is available only at index 5.
    mock_pat = 0x0706000607040406ULL;

    uint64_t flags = UINT64_MAX;

    TEST_ASSERT_TRUE(
        vm_get_memory_type_flags(VM_MEMORY_UC, &flags)
    );

    TEST_ASSERT_EQUAL_HEX64(
        PAGE_PAT | PAGE_PWT,
        flags
    );
}

void test_vm_pat_memory_type_unavailable(void)
{
    // Architectural default PAT does not contain WC.
    mock_pat = 0x0007040600070406ULL;

    uint64_t flags = UINT64_MAX;

    TEST_ASSERT_FALSE(
        vm_get_memory_type_flags(VM_MEMORY_WC, &flags)
    );

    // Failed lookups must not modify the output.
    TEST_ASSERT_EQUAL_HEX64(UINT64_MAX, flags);
}

void test_vm_pat_find_write_back(void)
{
    uint64_t flags = UINT64_MAX;

    // WB is available at index 0 in the default PAT.
    TEST_ASSERT_TRUE(
        vm_get_memory_type_flags(VM_MEMORY_WB, &flags)
    );

    TEST_ASSERT_EQUAL_HEX64(0, flags);
}

void test_vm_pat_unsupported_cpu(void)
{
    // Simulate a CPU without PAT support.
    mock_cpuid_edx &= ~CPUID_EDX_PAT;

    uint64_t flags = UINT64_MAX;

    TEST_ASSERT_FALSE(
        vm_get_memory_type_flags(VM_MEMORY_UC, &flags)
    );

    // The output must remain unchanged.
    TEST_ASSERT_EQUAL_HEX64(UINT64_MAX, flags);

    // An unsupported PAT register must never be accessed.
    TEST_ASSERT_EQUAL_UINT32(0, mock_read_msr_count);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_vm_pat_null_output);
    RUN_TEST(test_vm_pat_find_uncacheable);
    RUN_TEST(test_vm_pat_find_uncacheable_custom);
    RUN_TEST(test_vm_pat_memory_type_unavailable);
    RUN_TEST(test_vm_pat_find_write_back);
    RUN_TEST(test_vm_pat_unsupported_cpu);

    return UNITY_END();
}