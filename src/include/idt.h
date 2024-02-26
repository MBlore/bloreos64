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
#ifndef _BLOREOS_IDT_H
#define _BLOREOS_IDT_H

#include <stdint.h>
#include <stddef.h>

// Reserved IDT vectors.
#define TIMER_VECTOR 32
#define KEYBOARD_VECTOR 33
#define MOUSE_VECTOR 34
#define LAPICTMR_VECTOR 45

struct idt_entry
{
    uint16_t base_low;          // Handler location bits 0..15
    uint16_t selector;          // Codesegment descriptor selection
    uint8_t ist;                // IST offset in bits 0..2
    uint8_t flags;              // Segment selector flags
    uint16_t base_mid;          // Handler location bits 16..31
    uint32_t base_high;         // Handler location bits 32..63
    uint32_t reserved; 
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct idt_frame {
    uint64_t rip;
    uint64_t cs;
    uint64_t flags;
    uint64_t rsp;
    uint64_t ss;
};

void idt_init();

#endif