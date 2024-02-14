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
#include <ps2.h>
#include <cpu.h>
#include <lapic.h>
#include <idt.h>
#include <ioapic.h>
#include <stdbool.h>
#include <str.h>

#define PORT_PS2_CONFIG (uint8_t)0x20
#define PORT_PS2_DATA (uint16_t)0x60
#define PORT_PS2_STATUSCMD (uint16_t)0x64

uint8_t ps2_read() {
    // Wait for the status of the port to be not busy for reading.
    while ((inb(PORT_PS2_STATUSCMD) & 1) == 0);

    return inb(0x60);
}

void ps2_write(uint16_t port, uint8_t value) {
    // Wait for the status of the port to be not busy for writing.
    while ((inb(PORT_PS2_STATUSCMD) & 2) != 0);

    outb(port, value);
}

/*
uint8_t ps2_read_key()
{
    uint8_t status = inb(PORT_PS2_STATUSCMD);

    if (status & 1) {
        // We have key data to read.
        return inb(PORT_PS2_DATA);
    }

    return 0;
}
*/

/*
 * Reads flags about the state of the PS2 device. 
*/
uint8_t ps2_read_config()
{
    ps2_write(PORT_PS2_STATUSCMD, PORT_PS2_CONFIG);
    return ps2_read();
}

/*
 * Writes new config value to the PS2 device.
*/
void ps2_write_config(uint8_t value)
{
    ps2_write(PORT_PS2_STATUSCMD, PORT_PS2_DATA);
    ps2_write(PORT_PS2_DATA, value);
}

void ps2_init()
{
    // Initialization taken from: https://wiki.osdev.org/%228042%22_PS/2_Controller
    
    // Disable devices so initialization isn't interrupted.
    ps2_write(PORT_PS2_STATUSCMD, (uint8_t)0xAD);
    ps2_write(PORT_PS2_STATUSCMD, (uint8_t)0xA7);

    // Enable interrupt and scan-code translation config.
    uint8_t config = ps2_read_config();
    config |= (1 << 0) | (1 << 6);

    // Enable mouse interrupt if its present.
    if ((config & (1 << 5)) != 0) {
       config |= (1 << 1); 
    }

    // Enable the keyboard.
    ps2_write(PORT_PS2_STATUSCMD, 0xAE);

    // Enable the mouse if present.
    if ((config & (1 << 5)) != 0) {
        ps2_write(PORT_PS2_STATUSCMD, 0xA8);
    }

    // Setup the IRQ1 redirect in the I/O APIC to come to our keyboard vector in the IDT.
    // TODO: Get the SMP from Limine.
    ioapic_redirect_irq(lapic_id, KEYBOARD_VECTOR, 1, true);

    kprintf("PS2 initialized.\n");
}
