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

//#define HPET_DEBUG

#define FEMTOSECS_PER_SEC 1000000000000000LL

// Offsets from the base address of the HPET.
#define HPET_REG_MAIN_COUNTER       0x0F0
#define HPET_REG_TIMER_CFG          0x100
#define HPET_REG_TIMER_SIZE         0x20
#define HPET_REG_TIMER_COMP         0x108
#define HPET_REG_TIMER_COMP_SIZE    0x20

// HPET Timer Configuration Register, bit 2:
// Enables interrupt generation for this timer.
#define HPET_TIMER_INT_ENABLE (1ULL << 2)

// HPET Timer Configuration Register, bit 3:
// 0 = One-shot mode
// 1 = Periodic mode
#define HPET_TIMER_PERIODIC (1ULL << 3)

/*
 * HPET Timer Configuration Register:
 * Bits 9-13 select the I/O APIC interrupt route.
 *
 * The selected route must be supported by the timer's
 * interrupt routing capabilities (bits 32-63).
 */
#define HPET_TIMER_ROUTE_SHIFT 9U
#define HPET_TIMER_ROUTE_MASK  (0x1FULL << HPET_TIMER_ROUTE_SHIFT)

#define HPET_IOAPIC_FLAGS 0x5U

/*
 * HPET Timer Configuration Register, bit 1:
 * 0 = Edge-triggered interrupt
 * 1 = Level-triggered interrupt
 */
#define HPET_TIMER_INT_TYPE (1ULL << 1)

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

volatile uint64_t* _timer_config_reg(uint32_t n)
{
    return (uint64_t*)((char*)base_addr + (HPET_REG_TIMER_CFG + (HPET_REG_TIMER_SIZE * n)));
}

volatile uint64_t* _timer_comparator_reg(uint32_t n)
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

#ifdef HPET_DEBUG
    kprintf("HPET ACPI Config:\n");
    kprintf("  HPET Comparator Count: %d\n", hpet->comparator_count);
    kprintf("  HPET Counter Size: %d\n", hpet->counter_size);
    kprintf("  HPET Min Tick: %d\n", hpet->minimum_tick);
    kprintf("  HPET Legacy Replacement: %d\n", hpet->legacy_replacement);
#endif
    base_addr = (volatile uint64_t*)(hpet->address.address + vmm_higher_half_offset);
    kprintf("  Base: 0x%X\n", base_addr);

    hpet_regs = (volatile struct hpet_regs*)base_addr;
    hpet_main_counter = (volatile uint64_t*)((char*)base_addr + HPET_REG_MAIN_COUNTER);

    kprintf("HPET Capabilites:\n");
    uint64_t numTimers = ((hpet_regs->capabilities >> 8) & 0x1F) + 1;
    kprintf("  HPET Num Timers: %d\n", numTimers);
    kprintf("  HPET Legacy Route Capable: %d\n", (hpet_regs->capabilities >> 15) & 1);
    kprintf("  HPET 64-Bit Counter: %d\n", (hpet_regs->capabilities >> 13) & 1);
    uint32_t tick_period = (uint32_t)(hpet_regs->capabilities >> 32);
    kprintf("  HPET Tick Period: %d\n", tick_period);

    // The HPET counter period must be non-zero.
    if (tick_period == 0) {
        kprintf("HPET: Invalid tick period\n");
        return;
    }

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
#ifdef HPET_DEBUG
    kprintf("HPET Timer 0 Allowed Interrupt Routing: %s\n", buff);

    kprintf("HPET Timer 0 - Periodic: %d\n", *timer_config >> 4 & 1);
    kprintf("HPET Timer 0 - 64-Bit Mode: %d\n", *timer_config >> 5 & 1);
#endif
    // Set I/O APIC routing.
    uint8_t selected_ioapic_input = 2;  // // Temporary: QEMU's supported GSI 2.

    /*
     * Each bit in routing_caps identifies a supported
     * interrupt route. Bit N corresponds to GSI N.
     *
     * Reject routes that the HPET cannot generate.
     */
    if (!(routing_caps & (1U << selected_ioapic_input))) {
        kprintf("HPET: Selected interrupt route is unsupported\n");
        return;
    }

    /*
     * Verify that an I/O APIC handles the selected GSI.
     *
     * HPET support alone does not guarantee that an
     * I/O APIC exists for this interrupt route.
     */
    if (!ioapic_supports_gsi(selected_ioapic_input)) {
        kprintf("HPET: No I/O APIC handles the selected GSI\n");
        return;
    }

    *timer_config &= ~HPET_TIMER_ROUTE_MASK;
    *timer_config |= ((uint64_t)selected_ioapic_input << HPET_TIMER_ROUTE_SHIFT);

    // Start Timer 0 in one-shot mode.
    *timer_config &= ~HPET_TIMER_PERIODIC;

    // Use edge-triggered interrupts to match our IOAPIC configuration.
    *timer_config &= ~HPET_TIMER_INT_TYPE;

    // Allow setting the accumulator.
    //*timer_config |= (1 << 6);

    /*
     * Keep Timer 0 interrupts disabled during initialisation.
     *
     * hpet_one_shot() will enable them after programming
     * the comparator.
     */
    *timer_config &= ~HPET_TIMER_INT_ENABLE;

    /*
     * HPET uses a directly selected Global System Interrupt (GSI).
     * Flags 0x5:
     *   Bits 0-1 = 01 (active-high polarity)
     *   Bits 2-3 = 01 (edge-triggered)
     */
    if (!ioapic_redirect_gsi(
            bsp_lapic_id,
            TIMER_VECTOR,
            selected_ioapic_input,
            HPET_IOAPIC_FLAGS,
            true)) {
        kprintf("HPET: Failed to configure interrupt routing\n");
        return;
    }

    kprintf("HPET: Timer 0 routed to GSI %d\n", selected_ioapic_input);

    _ticks = 0;
    
    /*
     * Start the HPET main counter as a continuous clock source.
     *
     * The counter should run throughout the lifetime of the
     * kernel, independently of timer interrupt generation.
     */
    *hpet_main_counter = 0;
    hpet_enable();
}

/*
 * Configure Timer 0 to generate a single interrupt after
 * the specified number of milliseconds.
 *
 * The main counter continues running uninterrupted.
 */
void hpet_one_shot(uint64_t ms)
{
    volatile uint64_t *timer_config = _timer_config_reg(0);
    volatile uint64_t *timer_comp = _timer_comparator_reg(0);

    // Disable timer interrupts while configuring the comparator.
    *timer_config &= ~HPET_TIMER_INT_ENABLE;

    // Reset the software interrupt counter.
    _ticks = 0;

    // Configure one-shot mode.
    *timer_config &= ~HPET_TIMER_PERIODIC;

    // Calculate the number of ticks required for the delay.
    uint64_t duration = (hpet_frequency / 1000) * ms;

    // Set the comparator relative to the current main counter.
    *timer_comp = hpet_get_counter() + duration;

    // Enable timer interrupts.
    *timer_config |= HPET_TIMER_INT_ENABLE;
}

/* 
 * Disable the global enable for the HPET.
*/
inline void hpet_disable()
{
    #ifdef HPET_DEBUG
    kprintf("hpet_disable()\n");
    #endif
    hpet_regs->config &= ~1;
}

/* 
 * Enable the global enable for the HPET.
*/
inline void hpet_enable()
{
    #ifdef HPET_DEBUG
    kprintf("hpet_enable()\n");
    #endif
    hpet_regs->config |= 1;
}

/*
 * Disabes the HPET and resets the ticks tracker.
*/
void hpet_reset()
{
    #ifdef HPET_DEBUG
    kprintf("hpet_reset()\n");
    #endif
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
    //regs->interrupt_status |= 1;
    hpet_regs->interrupt_status = 1ULL;
}

/*
 * Sleep until the specified time in milliseconds has passed.
 * This uses an interrupt signal to denote end of the sleep period.
*/
void hpet_sleep(uint64_t ms)
{
    #ifdef HPET_DEBUG
    kprintf("hpet_sleep()\n");
    #endif
    hpet_one_shot(ms);

    // Wait until the ISR fires and we see a tick.
    while(_ticks == 0);
    
    // Disable Timer 0 interrupts, but keep the HPET main counter running.
    volatile uint64_t *timer_config = _timer_config_reg(0);
    *timer_config &= ~HPET_TIMER_INT_ENABLE;
}

/*
 * Sleep until the specified time in milliseconds has passed.
 * This uses a constant main counter read to detect the period has passed.
*/
void hpet_sleep_counter(uint64_t ms)
{
    // Record the current counter value.
    uint64_t start = hpet_get_counter();

    // Calculate the number of ticks required for the delay.
    uint64_t duration = (hpet_frequency / 1000) * ms;

    // Wait until the required number of ticks has elapsed.
    while ((hpet_get_counter() - start) < duration);
}

/*
 * Called when the HPET interrupt fires.
*/
void hpet_isr()
{
    _ticks++;
}

/*
 * Returns the current HPET main counter value.
 */
uint64_t hpet_get_counter()
{
    return *hpet_main_counter;
}

/*
 * Return the elapsed time in milliseconds since
 * the HPET main counter was initialised.
 */
uint64_t hpet_get_uptime_ms(void)
{
    return hpet_get_counter() / (hpet_frequency / 1000);
}