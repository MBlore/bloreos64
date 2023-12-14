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
    Everything to manage the IDT.
*/
#include <idt.h>

#define KERNEL_CODE_SEGMENT_OFFSET 0x08 // TODO: Check this

#define INTERRUPT_GATE 0xE;
#define TRAP_GATE 0xF;

struct idt_entry_64 idt[256];

struct idt_ptr {
    uint64_t limit;
    uint64_t base;
} __attribute__((packed));

struct idt_ptr idtp;

void idt_load()
{
    idtp.limit = sizeof(idt) - 1;;
    idtp.base = (uint64_t)idt;
    asm volatile("lidt %0" : : "m"(idtp) : "memory");
}

/* Registers a handler at the specified IDT index */
void idt_set_gate(int n, uint64_t handler)
{
    idt[n].offset_1 = handler & 0xFFFF;
    idt[n].offset_2 = (handler >> 16) & 0xFFFF;
    idt[n].offset_3 = (handler >> 32) & 0xFFFFFFFF;
    idt[n].selector = KERNEL_CODE_SEGMENT_OFFSET;
    idt[n].type_attr = INTERRUPT_GATE;
    idt[n].ist = 0;
    idt[n].reserved = 0;
}