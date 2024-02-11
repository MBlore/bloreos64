/*
    BloreOS - Operating System
    Copyright (C) 2023 Martin Blore

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
/*
    Everything to manage the APIC.
*/
#include <apic.h>
#include <mem.h>
#include <cpu.h>
#include <str.h>

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_ID_OFFSET 0x20
#define APIC_VERSION_OFFSET 0x30

uint64_t apic_base;

uint32_t _check_local_apic_cpuid() {
    uint32_t eax, edx;
    cpuid(1, &eax, &edx);
    return (edx >> 9) & 1;
}

static inline uint32_t apic_read(uint32_t offset)
{
    return *((volatile uint32_t*)(vmm_higher_half_offset + apic_base + offset));
}

static inline void apic_write(uint32_t offset, uint32_t val)
{
    *((volatile uint32_t*)(vmm_higher_half_offset + apic_base + offset)) = val;
}

void apic_init()
{
    apic_base = read_msr(IA32_APIC_BASE_MSR);
    
    // Mask out the flags to get only the address.
    apic_base = (apic_base & 0xFFFFF000);

    kprintf("APIC Base: %X\n", apic_base);

    uint32_t apic_id = apic_read(APIC_ID_OFFSET);
    uint32_t apic_version = apic_read(APIC_VERSION_OFFSET);

    kprintf("APIC ID: 0x%X\n", apic_id);
    kprintf("APIC Version: 0x%X\n", apic_version);

    // Get enabled flag.
    volatile uint32_t* apic_reg = (volatile uint32_t*)(vmm_higher_half_offset + apic_base);
    uint32_t apic_flags = *apic_reg;

    kprintf("CPUID APIC Enabled: %d\n", _check_local_apic_cpuid());
    kprintf("MSR APIC Enabled: %d\n", apic_flags);
}
