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
#ifndef _BLOREOS_CPU_H
#define _BLOREOS_CPU_H

#include <stdint.h>
#include <str.h>
#include <stdbool.h>

extern uint32_t bsp_lapic_id;      // Bootstrap processor APIC ID.
extern uint64_t cpu_count;

extern void cpu_init();

/* Sends a 8-bit value to a I/O location */
static inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "nd"(port) : "memory");
}

/* Sends a 16-bit value to a I/O location */
static inline void outw(uint16_t port, uint16_t val)
{
    asm volatile("outw %0, %1" : : "a"(val), "nd"(port) : "memory");
}

/* Sends a 32-bit value to a I/O location */
static inline void outl(uint16_t port, uint32_t val)
{
    asm volatile("outl %0, %1" : : "a"(val), "nd"(port) : "memory");
}

/* Receives a 8-bit value form an I/O location */
static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0"
                 : "=a"(ret)
                 : "Nd"(port)
                 : "memory");
    return ret;
}

/* Receives a 16-bit value form an I/O location */
static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    asm volatile("inw %1, %0"
                 : "=a"(ret)
                 : "Nd"(port)
                 : "memory");
    return ret;
}

/* Receives a 32-bit value form an I/O location */
static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    asm volatile("inl %1, %0"
                 : "=a"(ret)
                 : "Nd"(port)
                 : "memory");
    return ret;
}

static inline uint64_t get_rax()
{
    uint64_t val;
    asm volatile ("movq %%rax, %0" : "=r"(val) :: "memory");
    return val;
}

static inline void set_rax(uint64_t val)
{
    asm volatile ("movq %0, %%rax" :: "r"(val) : "memory");
}

static inline uint64_t read_msr(uint32_t msr_id)
{
    uint32_t low, high;

    asm volatile (
        "rdmsr" :
        "=a"(low), "=d"(high) :
        "c"(msr_id)
    );

    return ((uint64_t) high << 32) | low;
}

/*
    Control registers, only available in ring-0.
    CR0 - System control flags
        [31] - Paging enabled
        [30] - Cache Disable
    CR1 - Reserved
    CR2 - Page fault address
    CR3 - Paging hierarchy address
    CR4 - Extension flags
    CR8 - Task Priority Register (TPR) priority threshold 
*/
static inline uint64_t get_cr0()
{
    uint64_t val;
    asm volatile ("movq %%cr0, %0" : "=r"(val) :: "memory");
    return val;
}

static inline void cpuid(int code, uint32_t *a, uint32_t *d)
{
    asm volatile (
        "cpuid"
        : "=a"(*a), "=d"(*d)
        : "O"(code)
        : "ebx", "ecx"
        );
}

static inline int is_paging_enabled()
{
    // Check for bit 31
    uint64_t bitmask = 0x80000000;

    if (get_cr0() & bitmask) {
        return 1;
    }

    return 0;
}

static inline void set_cr0(uint64_t val)
{
    asm volatile ("movq %0, %%cr0" :: "r"(val) : "memory");
}

static inline void lidt(void* idt_ptr)
{
    asm volatile("lidt (%0)" :: "r"(idt_ptr) : "memory");
}

static inline void disable_interrupts()
{
    asm("cli");
}

static inline void enable_interrupts()
{
    asm("sti");
}

static inline bool interrupt_state()
{
    unsigned long flags;

    asm volatile ("pushfq\n\t"
                  "pop %0\n\t"
                  : "=rm" (flags)
                  :
                  : "memory");

    return (flags & (1 << 9)) != 0;
}

/*
 * Sets the interrupt state and returns the state of interrupts before the change.
*/
static inline bool set_interrupt_state(bool enabled)
{
    bool state = interrupt_state();

    if (enabled) {
        enable_interrupts();
    } else {
        disable_interrupts();
    }

    return state;
}



/*
 * Interrupt service routine state save.
*/
static inline void isr_save()
{
    asm(
        "push %%r15\n\t"
        "push %%r14\n\t"
        "push %%r13\n\t"
        "push %%r12\n\t"
        "push %%r11\n\t"
        "push %%r10\n\t"
        "push %%r9\n\t"
        "push %%r8\n\t"
        "push %%rbp\n\t"
        "push %%rdi\n\t"
        "push %%rsi\n\t"
        "push %%rdx\n\t"
        "push %%rcx\n\t"
        "push %%rbx\n\t"
        "push %%rax\n\t"
        "mov %%es, %%eax\n\t"
        "push %%rax\n\t"
        "mov %%ds, %%eax\n\t"
        "push %%rax\n\t"
        :
        :
        : "memory"
    );
}

/*
 * Interrupt service routine state restore.
*/
static inline void isr_restore()
{
    asm(
        "pop %%rax\n\t"
        "mov %%eax, %%ds\n\t"
        "pop %%rax\n\t"
        "mov %%eax, %%es\n\t"
        "pop %%rax\n\t"
        "pop %%rbx\n\t"
        "pop %%rcx\n\t"
        "pop %%rdx\n\t"
        "pop %%rsi\n\t"
        "pop %%rdi\n\t"
        "pop %%rbp\n\t"
        "pop %%r8\n\t"
        "pop %%r9\n\t"
        "pop %%r10\n\t"
        "pop %%r11\n\t"
        "pop %%r12\n\t"
        "pop %%r13\n\t"
        "pop %%r14\n\t"
        "pop %%r15\n\t"
        "add $8, %%rsp\n\t"
        "iretq"
        :
        :
        : "memory"
    );
}

#endif