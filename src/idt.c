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
#include <idt.h>
#include <cpu.h>
#include <str.h>
#include <string.h>
#include <lapic.h>
#include <ps2.h>
#include <terminal.h>
#include <kernel.h>
#include <serial.h>
#include <hpet.h>

#define KERNEL_CODE_SEGMENT_OFFSET 0x08 // TODO: Check this

#define INTERRUPT_GATE 0xE;
#define TRAP_GATE 0xF;

struct idt_entry idt[256];
struct idt_ptr idtp;

extern void ISR_Handler_PS2(void);
extern void ISR_Handler_Faults(void);

extern void *isr_thunks[];

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
    idt[vector].selector = 0x08; // Code segment in the GDT.
    idt[vector].flags = flags;
    idt[vector].ist = 0;
    idt[vector].reserved = 0;
}

void _handle_fault(uint64_t vector)
{
    const char *fault_names[32];
    fault_names[0] = "Divide Error Exception";
    fault_names[1] = "Debug Exception";
    fault_names[2] = "NMI Interrupt";
    fault_names[3] = "Breakpoint Exception";
    fault_names[4] = "Overflow Exception";
    fault_names[5] = "BOUND Range Exceeded Exception";
    fault_names[6] = "Invalid Opcode Exception";
    fault_names[7] = "Device Not Available Exception";
    fault_names[8] = "Double Fault Exception";
    fault_names[9] = "Coprocessor Segment Overrun";
    fault_names[10] = "Invalid TSS Exception";
    fault_names[11] = "Segment Not Present";
    fault_names[12] = "Stack Fault Exception";
    fault_names[13] = "General Protection Exception";
    fault_names[14] = "Page Fault Exception";
    fault_names[15] = "";
    fault_names[16] = "x87 FPU Floating-Point Error";
    fault_names[17] = "Alignment Check Exception";
    fault_names[18] = "Machine-Check Exception";
    fault_names[19] = "SIMD Floating-Point Exception";
    fault_names[20] = "Virtualization Exception";
    fault_names[21] = "Control Protection Exception";
    
    kprintf("**FAULT**: (%lu) %s\n", vector, fault_names[vector]);
}

/*
 * ISR Handler for the HPET timer 0.
*/
void _handle_timer()
{
    isr_save();
    hpet_isr();
    hpet_ack();
    lapic_eoi();
    isr_restore();
    __builtin_unreachable();
}

/*
 * Called from the isr.S handler function.
*/
void _handle_keyboard()
{
    uint8_t key = ps2_read_no_wait();
    
    cqueue_write(q_keyboard, key);
    
    lapic_eoi();
}

void idt_init()
{
    memset(&idt, 0, sizeof(idt));

    for (int i = 0; i < 32; i++) {
        // 0x8E = ring-0 privilege level.
        _idt_set_gate(i, isr_thunks[i], 0x8E);
    }

    // Device gates.
    _idt_set_gate(TIMER_VECTOR, _handle_timer, 0x8E);
    _idt_set_gate(KEYBOARD_VECTOR, ISR_Handler_PS2, 0x8E);

    _idt_load();
    kprintf("Loading IDT at: 0x%X\n", &idtp);
}