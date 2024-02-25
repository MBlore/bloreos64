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

// Offsets from the base address of the HPET.
#define HPET_REG_MAIN_COUNTER       0x0F0
#define HPET_REG_TIMER_CFG          0x100
#define HPET_REG_TIMER_SIZE         0x20
#define HPET_REG_TIMER_COMP         0x108
#define HPET_REG_TIMER_COMP_SIZE    0x20

struct hpet_regs {
    volatile uint64_t capabilities;      // Read-Only
    volatile uint64_t reserved1;
    volatile uint64_t config;            // Read/Write
    volatile uint64_t reserved2;
    volatile uint64_t interrupt_status;  // Interrupt status bit for timer (bit 0 timer 0, bit 1 timer 1 etc.).
};

volatile uint64_t *base_addr = 0;
volatile struct hpet_regs *hpet_regs = 0;
volatile uint64_t *hpet_main_counter = 0;

uint64_t hpet_frequency;
volatile uint64_t _ticks;       // How many times the timer interrupt has fired.

uint64_t* _timer_config_reg(uint32_t n)
{
    return (uint64_t*)((char*)base_addr + (HPET_REG_TIMER_CFG + (HPET_REG_TIMER_SIZE * n)));
}

uint64_t* _timer_comparator_reg(uint32_t n)
{
    return (uint64_t*)((char*)base_addr + (HPET_REG_TIMER_COMP + (HPET_REG_TIMER_COMP_SIZE * n)));
}

static inline uint64_t hpet_read(uint64_t offset)
{
    return *((volatile uint64_t*)((char*)base_addr + offset));
}

static inline void hpet_write(uint64_t offset, uint64_t val)
{
    *((volatile uint64_t*)((char*)base_addr + offset)) = val;
}

void hpet_init()
{
    kprintf("HPET Initializing...\n");

    kprintf("HPET ACPI Config:\n");
    kprintf("  HPET Comparator Count: %d\n", hpet->comparator_count);
    kprintf("  HPET Counter Size: %d\n", hpet->counter_size);
    kprintf("  HPET Min Tick: %d\n", hpet->minimum_tick);
    kprintf("  HPET Legacy Replacement: %d\n", hpet->legacy_replacement);

    base_addr = (volatile uint64_t*)(hpet->address.address + vmm_higher_half_offset);
    kprintf("  Base: 0x%X\n", base_addr);

    hpet_regs = (volatile struct hpet_regs*)base_addr;
    hpet_main_counter = (volatile uint64_t*)((char*)base_addr + HPET_REG_MAIN_COUNTER);

    kprintf("HPET Capabilites:\n");
    uint64_t numTimers = (hpet_regs->capabilities >> 8) & 0x1F;
    kprintf("  HPET Num Timers: %d\n", numTimers);
    kprintf("  HPET Legacy Route Capable: %d\n", (hpet_regs->capabilities >> 15) & 1);
    kprintf("  HPET 64-Bit Counter: %d\n", (hpet_regs->capabilities >> 13) & 1);
    uint32_t tick_period = (uint32_t)(hpet_regs->capabilities >> 32);
    kprintf("  HPET Tick Period: %d\n", tick_period);

    hpet_frequency = FEMTOSECS_PER_SEC / tick_period;
    kprintf("  HPET Frequency: %d\n", hpet_frequency);

    // If following is 1, routing is as follows (0 = no legacy routing):
    //  Timer 0 will be routed to IRQ0 in Non-APIC or IRQ2 in the I/O APIC.
    //  Timer 1 will be routed to IRQ8 in Non-APIC or IRQ8 in the I/O APIC.
    //  Timer 2-n will be routed as per the routing in the timer n config registers.
    //  The individual routing bits for timers 0 and 1 (APIC or FSB) will have no impact.
    kprintf("HPET Config:\n");
    uint8_t legacy_replace_route = (hpet_regs->config >> 1) & 1;
    kprintf("  HPET Legacy Replacement Route Supported: %d\n", legacy_replace_route);
    kprintf("  HPET Overall Enable: %d\n", hpet_regs->config & 1);

    // Configure timer 0.
    volatile uint64_t *timer_config = _timer_config_reg(0);

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

    // Enable periodic timer mode (default).
    *timer_config |= (1 << 3);

    // Allow setting the accumulator.
    //*timer_config |= (1 << 6);

    // Enable interrupts.
    *timer_config |= (1 << 2);

    // Set the IRQ IDT handler.
    ioapic_redirect_irq(bsp_lapic_id, TIMER_VECTOR, 0, true);

    _ticks = 0;
}

/*
 * Initialize timer 0 in one shot mode and start it.
*/
void hpet_one_shot(uint64_t ms)
{
    hpet_reset();

    // Reset the main counter to 0 for the next one-shot.
    *hpet_main_counter = 0;

    volatile uint64_t *timer_config = _timer_config_reg(0);
    *timer_config &= ~(1 << 3); // One shot mode.

    // Set the comparator value.
    volatile uint64_t* timer_comp = _timer_comparator_reg(0);
    *timer_comp = (uint64_t)((hpet_frequency / 1000) * ms);

    *timer_config |= (1 << 2);      // Enable timer interrupts.

    hpet_enable();
}

/* 
 * Disable the global enable for the HPET.
*/
inline void hpet_disable()
{
    hpet_regs->config &= ~1;
}

/* 
 * Enable the global enable for the HPET.
*/
inline void hpet_enable()
{
    hpet_regs->config |= 1;
}

/*
 * Disabes the HPET and resets the ticks tracker. 
*/
void hpet_reset()
{
    hpet_disable();
    _ticks = 0;
}

void hpet_ack()
{
    // Interrupt status depends on edge or level mode.
    // Level Mode:
    //   Write 1 to the timer position.
    // Edge mode (default):
    //   (Optional) Write 0 to the timer position n.
    volatile struct hpet_regs *regs = (volatile struct hpet_regs*)base_addr;
    //regs->interrupt_status |= 1;
    regs->interrupt_status &= ~((uint64_t)1);
}

/*
 * Sleep until the specified time in milliseconds has passed.
*/
void hpet_sleep(uint64_t ms)
{
    hpet_one_shot(ms);

    // Wait until the ISR fires and we see a tick.
    while(_ticks == 0);

    hpet_disable();
}

/*
 * Called when the HPET interrupt fires.
*/
void hpet_isr()
{
    _ticks++;
}