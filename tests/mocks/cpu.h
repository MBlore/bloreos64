#ifndef TEST_MOCK_CPU_H
#define TEST_MOCK_CPU_H

#include <stdint.h>

#define CR4_PAE     0x20
#define CR4_LA57    0x1000

// CPUID leaf for processor feature information.
#define CPUID_FEATURES 0x01

// PAT support flag in the CPUID feature EDX register.
#define CPUID_EDX_PAT (1U << 16)

extern uint32_t mock_cpuid_edx;

// Simulated CPU register values.
extern uint64_t mock_cr3;
extern uint64_t mock_cr4;

// Simulated physical and linear address widths.
extern uint64_t mock_address_widths;

// Simulated paging state.
extern int mock_paging_enabled;

// Simulated Page Attribute Table register.
#define IA32_PAT 0x277

extern uint64_t mock_pat;
extern uint32_t mock_read_msr_count;

static inline void cpuid(int code, uint32_t *a, uint32_t *d)
{
    *a = 0;
    *d = 0;

    if (code == CPUID_FEATURES) {
        *d = mock_cpuid_edx;
    }
}

static inline uint64_t read_msr(uint32_t msr_id)
{
    mock_read_msr_count++;
    
    if (msr_id == IA32_PAT) {
        return mock_pat;
    }

    return 0;
}

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