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

#define LAPIC_APICID        0x20
#define LAPIC_APICVER       0x30
#define LAPIC_TPR           0x80  // (Task Priority Register)
#define LAPIC_EOI           0x0B0
#define LAPIC_LDR           0x0D0
#define LAPIC_DFR           0x0E0
#define LAPIC_SPURIOUS      0x0F0
#define LAPIC_ESR           0x280
#define LAPIC_ICRL          0x300
#define LAPIC_ICRH          0x310
#define LAPIC_LVT_TMR       0x320
#define LAPIC_LVT_THERM     0x330
#define LAPIC_LVT_PERF      0x340
#define LAPIC_LVT_LINT0     0x350
#define LAPIC_LVT_LINT1     0x360
#define LAPIC_LVT_ERR       0x370
#define LAPIC_TMRINITCNT    0x380
#define LAPIC_TMRCURRCNT    0x390
#define LAPIC_TMRDIV        0x3E0
#define LAPIC_LAST          0x38F
#define LAPIC_DISABLE       0x10000
#define LAPIC_SW_ENABLE     0x100
#define LAPIC_CPUFOCUS      0x200
#define LAPIC_NMI           (4 << 8)
#define TMR_PERIODC         0x20000
#define TMR_BASEDIV         (1 << 20)

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

    lapic_id = lapic_read(LAPIC_APICID);
    uint32_t apic_version = lapic_read(LAPIC_APICVER);

    kprintf("LAPIC ID: %d\n", lapic_id);
    kprintf("LAPIC Version: %d\n", apic_version);

    // Get enabled flag.
    volatile uint32_t* apic_reg = (volatile uint32_t*)(vmm_higher_half_offset + apic_base);
    uint32_t apic_flags = *apic_reg;

    kprintf("CPUID APIC Enabled: %d\n", _check_lapic_cpuid());
    kprintf("MSR APIC Enabled: %d\n", apic_flags);

    // Set spurirous interrupt vector (at low byte), and enable the LAPIC at bit 8.
    lapic_write(LAPIC_SPURIOUS, (uint32_t)(0xFF & LAPIC_SW_ENABLE));

    // Reset the timer.
    lapic_write(LAPIC_LVT_TMR, LAPIC_DISABLE);
    lapic_write(LAPIC_TMRDIV, 0x3);
    lapic_write(LAPIC_TMRINITCNT, 0xFFFFFFFF);  // Sets the count to -1.
    
    uint32_t val = lapic_read(LAPIC_TMRCURRCNT);
    kprintf("LAPIC Counter: %d\n", val);
    val = lapic_read(LAPIC_TMRCURRCNT);
    kprintf("LAPIC Counter: %d\n", val);
}

/*
 * Raises an interrupt on the target CPU's LAPIC.
*/
void lapic_raiseint(uint32_t lapic_id, uint32_t vector)
{
    lapic_write(LAPIC_ICRH, lapic_id << 24);        // The LAPIC id is written at bits 24-27 in this reg.
    lapic_write(LAPIC_ICRL, vector);                // Bits 0-7 for the vector number. Other bits are flags.
}

/*
 * Clears the end of interrupt register bit - called at the end handling an interrupt in an ISR.
*/
void lapic_eoi()
{
    lapic_write(LAPIC_EOI, 0);
}
