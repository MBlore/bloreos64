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
#include <lapic.h>
#include <mem.h>
#include <cpu.h>
#include <str.h>

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_ID_OFFSET 0x20
#define APIC_VERSION_OFFSET 0x30

uint64_t apic_base;
uint32_t lapic_id;

uint32_t _check_lapic_cpuid() {
    uint32_t eax, edx;
    cpuid(1, &eax, &edx);
    return (edx >> 9) & 1;
}

static inline uint32_t lapic_read(uint32_t offset)
{
    return *((volatile uint32_t*)(vmm_higher_half_offset + apic_base + offset));
}

static inline void lapic_write(uint32_t offset, uint32_t val)
{
    *((volatile uint32_t*)(vmm_higher_half_offset + apic_base + offset)) = val;
}

void lapic_init()
{
    apic_base = read_msr(IA32_APIC_BASE_MSR);
    
    // Mask out the flags to get only the address.
    apic_base = (apic_base & 0xFFFFF000);

    kprintf("LAPIC Base: %X\n", apic_base);

    lapic_id = lapic_read(APIC_ID_OFFSET);
    uint32_t apic_version = lapic_read(APIC_VERSION_OFFSET);

    kprintf("LAPIC ID: %d\n", lapic_id);
    kprintf("LAPIC Version: %d\n", apic_version);

    // Get enabled flag.
    volatile uint32_t* apic_reg = (volatile uint32_t*)(vmm_higher_half_offset + apic_base);
    uint32_t apic_flags = *apic_reg;

    kprintf("CPUID APIC Enabled: %d\n", _check_lapic_cpuid());
    kprintf("MSR APIC Enabled: %d\n", apic_flags);
}

#define EOI_REGISTER_OFFSET 0x0B0
void lapic_eoi()
{
    lapic_write(EOI_REGISTER_OFFSET, 0);   
}
