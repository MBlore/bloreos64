#ifndef TEST_MOCK_KERNEL_H
#define TEST_MOCK_KERNEL_H

#include <stdlib.h>
#include <setjmp.h>

extern jmp_buf mock_hcf_context;
extern int mock_hcf_expected;

static inline void hcf(void)
{
    // Return control to the test if a kernel halt is expected.
    if (mock_hcf_expected) {
        longjmp(mock_hcf_context, 1);
    }

    // Unexpected kernel halts terminate the test process.
    abort();
}

#endif