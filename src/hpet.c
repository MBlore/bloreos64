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

struct hpet_regs {
    uint64_t capabilities;      // Read-Only
    uint64_t reserved1;
    uint64_t config;            // Read/Write
    uint64_t reserved2;
    uint64_t interrupt_status;  // Read/Write Clear
};

uint64_t *base_addr = 0;

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

    kprintf("HPET Comparator Count: %d\n", hpet->comparator_count);
    kprintf("HPET Counter Size: %d\n", hpet->counter_size);
    kprintf("HPET Min Tick: %d\n", hpet->minimum_tick);
    kprintf("HPET Legacy Replacement: %d\n", hpet->legacy_replacement);

    base_addr = (uint64_t*)(hpet->address.address + vmm_higher_half_offset);
    struct hpet_regs *regs = (struct hpet_regs*)base_addr;

    uint64_t numTimers = (regs->capabilities >> 8) & 0x1F;
    kprintf("HPET Num Timers: %d\n", numTimers);
    kprintf("HPET 64-Bit Mode: %d\n", (regs->capabilities >> 13) & 1);
    
    uint32_t tick_period = (uint32_t)(regs->capabilities >> 32);
    kprintf("HPET Tick Period: %d\n", tick_period);

    kprintf("HPET Overall Enable: %d\n", regs->config & 1);

    uint64_t *main_counter_reg = (uint64_t*)((char*)base_addr + 0xF0);
    kprintf("HPET Main Counter: %d\n", *main_counter_reg);

    // Configure the timer.
    uint64_t *timer_config = _timer_config_reg(0);

    kprintf("HPET Timer 0 - Periodic: %d\n", *timer_config >> 4 & 1);
    kprintf("HPET Timer 0 - 64-Bit Mode: %d\n", *timer_config >> 5 & 1);

    // Set IRQ for timer N. Using IRQ4 - 36th bit.
    //*timer_config |= (1 << 36);

    // Enable I/O API routing.
    *timer_config |= (1 << 9);

    // Allow setting the accumulator.
    *timer_config |= (1 << 6);

    // Enable periodic timer mode.
    *timer_config |= (1 << 3);

    // Enable interrupts.
    *timer_config |= (1 << 2);

    // Set level interrupt mode to use level, not edge.
    *timer_config |= (1ULL << 1);

    // Disable the PIT timer on IRQ0.
    disable_interrupts();
    outb(0x43, 0x30);
    outb(0x40, 0);
    outb(0x40, 0);
    enable_interrupts();

    // Set the comparator.
    uint64_t* timer_comp = _timer_comparator_reg(0);
    *timer_comp = tick_period * 10;
    *timer_comp = tick_period * 10; // This sets the accumulator.

    // Set the IRQ IDT handler.
    ioapic_redirect_irq(bsp_lapic_id, TIMER_VECTOR, 0, true);

    // Global enable.
    regs->config = regs->config | 1;
}

void hpet_ack()
{
    struct hpet_regs *regs = (struct hpet_regs*)base_addr;
    regs->interrupt_status |= (1 << 0);
}