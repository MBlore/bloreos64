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
#include <cpu.h>
#include <str.h>

#define KERNEL_CODE_SEGMENT_OFFSET 0x08 // TODO: Check this

#define INTERRUPT_GATE 0xE;
#define TRAP_GATE 0xF;

struct idt_entry idt[256];
struct idt_ptr idtp;

void _idt_load()
{
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint64_t)&idt;
    lidt(&idtp);
}

/* Registers a handler at the specified IDT index */
void _idt_set_gate(int vector, void *handler, uint8_t flags)
{
    uint64_t ihandler = (uint64_t)handler;

    idt[vector].base_low = (uint16_t)ihandler;
    idt[vector].base_mid = (uint16_t)(ihandler >> 16),
    idt[vector].base_high = (uint32_t)(ihandler >> 32),
    idt[vector].selector = 0x28;
    idt[vector].flags = flags;
    idt[vector].ist = 0;
    idt[vector].reserved = 0;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
void _handle_interrupt()
{
    // TODO: We're not getting here :(.
    kprintf("**EXCEPTION**: Division by zero.");
}
#pragma GCC diagnostic pop

void init_idt()
{
    for (int i = 0; i < 32; i++) {
        _idt_set_gate(i, _handle_interrupt, 0x8E);
    }
    _idt_load();
    kprintf("Loading IDT at: %X", &idtp);
}