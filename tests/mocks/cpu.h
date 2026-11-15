#ifndef TEST_MOCK_CPU_H
#define TEST_MOCK_CPU_H

#include <stdint.h>

#define CR4_PAE     0x20
#define CR4_LA57    0x1000

// Simulated CPU register values.
extern uint64_t mock_cr3;
extern uint64_t mock_cr4;

// Simulated physical and linear address widths.
extern uint64_t mock_address_widths;

// Simulated paging state.
extern int mock_paging_enabled;

static inline uint64_t get_cr3(void)
{
    return mock_cr3;
}

static inline uint64_t get_cr4(void)
{
    return mock_cr4;
}

static inline uint64_t cpu_get_address_widths(void)
{
    return mock_address_widths;
}

static inline int is_paging_enabled(void)
{
    return mock_paging_enabled;
}

void invlpg(uint64_t virt_addr);

#endif