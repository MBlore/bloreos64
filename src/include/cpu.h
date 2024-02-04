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

static inline void lgdt(void* gdt_ptr)
{
    asm volatile ("lgdt (%0)" :: "r"(gdt_ptr) : "memory");
}

#endif