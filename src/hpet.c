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
#include <stdint.h>
#include <hpet.h>
#include <acpi.h>
#include <str.h>
#include <mem.h>
#include <ioapic.h>
#include <idt.h>
#include <cpu.h>

#define FEMTOSECS_PER_SEC 1000000000000000LL

struct hpet_regs {
    uint64_t capabilities;      // Read-Only
    uint64_t reserved1;
    uint64_t config;            // Read/Write
    uint64_t reserved2;
    uint64_t interrupt_status;  // Interrupt status bit for timer (bit 0 timer 0, bit 1 timer 1 etc.).
};

uint64_t *base_addr = 0;
uint64_t hpet_frequency;

uint64_t* _timer_config_reg(uint32_t n)
{
    return (uint64_t*)((char*)base_addr + (0x100 + (0x20 * n)));
}

uint64_t* _timer_comparator_reg(uint32_t n)
{
    return (uint64_t*)((char*)base_addr + (0x108 + (0x20 * n)));
}

void hpet_init()
{
    kprintf("HPET Initializing...\n");

    kprintf("HPET ACPI Config:\n");
    kprintf("  HPET Comparator Count: %d\n", hpet->comparator_count);
    kprintf("  HPET Counter Size: %d\n", hpet->counter_size);
    kprintf("  HPET Min Tick: %d\n", hpet->minimum_tick);
    kprintf("  HPET Legacy Replacement: %d\n", hpet->legacy_replacement);

    base_addr = (uint64_t*)(hpet->address.address + vmm_higher_half_offset);
    struct hpet_regs *regs = (struct hpet_regs*)base_addr;

    kprintf("HPET Capabilites:\n");
    uint64_t numTimers = (regs->capabilities >> 8) & 0x1F;
    kprintf("  HPET Num Timers: %d\n", numTimers);
    kprintf("  HPET Legacy Route Capable: %d\n", (regs->capabilities >> 15) & 1);
    kprintf("  HPET 64-Bit Counter: %d\n", (regs->capabilities >> 13) & 1);
    uint32_t tick_period = (uint32_t)(regs->capabilities >> 32);
    kprintf("  HPET Tick Period: %d\n", tick_period);

    hpet_frequency = FEMTOSECS_PER_SEC / tick_period;
    kprintf("  HPET Frequency: %d\n", hpet_frequency);

    // If following is 1, routing is as follows (0 = no legacy routing):
    //  Timer 0 will be routed to IRQ0 in Non-APIC or IRQ2 in the I/O APIC.
    //  Timer 1 will be routed to IRQ8 in Non-APIC or IRQ8 in the I/O APIC.
    //  Timer 2-n will be routed as per the routing in the timer n config registers.
    //  The individual routing bits for timers 0 and 1 (APIC or FSB) will have no impact.
    kprintf("HPET Config:\n");
    uint8_t legacy_replace_route = (regs->config >> 1) & 1;
    kprintf("  HPET Legacy Replacement Route Supported: %d\n", legacy_replace_route);
    kprintf("  HPET Overall Enable: %d\n", regs->config & 1);

    uint64_t *main_counter_reg = (uint64_t*)((char*)base_addr + 0xF0);
    kprintf("HPET Main Counter: %d\n", *main_counter_reg);

    // Configure timer 0.
    uint64_t *timer_config = _timer_config_reg(0);

    // Report allowed interrupt routings.
    // If bit 0 is on, it means timer can be routed on Interrupt 0.
    // If bit 1 is on, it means timer can be routed on Interrupt 1 etc.
    uint32_t routing_caps = (uint32_t)(*timer_config >> 32);
    char buff[36];
    sprint_binary32(buff, routing_caps);
    kprintf("HPET Timer 0 Allowed Interrupt Routing: %s\n", buff);

    kprintf("HPET Timer 0 - Periodic: %d\n", *timer_config >> 4 & 1);
    kprintf("HPET Timer 0 - 64-Bit Mode: %d\n", *timer_config >> 5 & 1);

    // Set I/O APIC routing.
    uint8_t selected_ioapic_input = 0;  // Max value is 32.
    *timer_config |= (selected_ioapic_input << 9);

    // Enable periodic timer mode.
    *timer_config |= (1 << 3);

    // Allow setting the accumulator.
    *timer_config |= (1 << 6);

    // Enable interrupts.
    *timer_config |= (1 << 2);

    // Set the comparator.
    // Setting the comparator to the hpet_frequency is exactly 1 second.
    uint64_t* timer_comp = _timer_comparator_reg(0);
    *timer_comp = hpet_frequency / 2;
    *timer_comp = hpet_frequency / 2;    // Accumulator set.

    // Set the IRQ IDT handler.
    ioapic_redirect_irq(bsp_lapic_id, TIMER_VECTOR, 0, true);

    // Global enable - timers start.
    regs->config |= 1;
}

void hpet_ack()
{
    // Interrupt status depends on edge or level mode.
    // Level Mode:
    //   Write 1 to the timer position.
    // Edge mode (default):
    //   (Optional) Write 0 to the timer position n.
    struct hpet_regs *regs = (struct hpet_regs*)base_addr;
    //regs->interrupt_status |= 1;
    regs->interrupt_status &= ~((uint64_t)1);
}