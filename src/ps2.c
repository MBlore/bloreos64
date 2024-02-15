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

// UK Layout.
KeyEvent_t key_map[] = {
    // Top number row.
    { .scan_code = 2, .event_type = 0, .ascii = '1' },
    { .scan_code = 130, .event_type = 1, .ascii = '1' },
    { .scan_code = 3, .event_type = 0, .ascii = '2' },
    { .scan_code = 131, .event_type = 1, .ascii = '2' },
    { .scan_code = 4, .event_type = 0, .ascii = '3' },
    { .scan_code = 132, .event_type = 1, .ascii = '3' },
    { .scan_code = 5, .event_type = 0, .ascii = '4' },
    { .scan_code = 133, .event_type = 1, .ascii = '4' },
    { .scan_code = 6, .event_type = 0, .ascii = '5' },
    { .scan_code = 134, .event_type = 1, .ascii = '5' },
    { .scan_code = 7, .event_type = 0, .ascii = '6' },
    { .scan_code = 135, .event_type = 1, .ascii = '6' },
    { .scan_code = 8, .event_type = 0, .ascii = '7' },
    { .scan_code = 136, .event_type = 1, .ascii = '7' },
    { .scan_code = 9, .event_type = 0, .ascii = '8' },
    { .scan_code = 137, .event_type = 1, .ascii = '8' },
    { .scan_code = 10, .event_type = 0, .ascii = '9' },
    { .scan_code = 138, .event_type = 1, .ascii = '9' },
    { .scan_code = 11, .event_type = 0, .ascii = '0' },
    { .scan_code = 139, .event_type = 1, .ascii = '0' },
    { .scan_code = 12, .event_type = 0, .ascii = '-' },
    { .scan_code = 140, .event_type = 1, .ascii = '-' },
    { .scan_code = 13, .event_type = 0, .ascii = '=' },
    { .scan_code = 141, .event_type = 1, .ascii = '=' },
    
    // Q row.
    { .scan_code = 16, .event_type = 0, .ascii = 'q' },
    { .scan_code = 144, .event_type = 1, .ascii = 'q' },
    { .scan_code = 17, .event_type = 0, .ascii = 'w' },
    { .scan_code = 145, .event_type = 1, .ascii = 'w' },
    { .scan_code = 18, .event_type = 0, .ascii = 'e' },
    { .scan_code = 146, .event_type = 1, .ascii = 'e' },
    { .scan_code = 19, .event_type = 0, .ascii = 'r' },
    { .scan_code = 147, .event_type = 1, .ascii = 'r' },
    { .scan_code = 20, .event_type = 0, .ascii = 't' },
    { .scan_code = 148, .event_type = 1, .ascii = 't' },
    { .scan_code = 21, .event_type = 0, .ascii = 'y' },
    { .scan_code = 149, .event_type = 1, .ascii = 'y' },
    { .scan_code = 22, .event_type = 0, .ascii = 'u' },
    { .scan_code = 150, .event_type = 1, .ascii = 'u' },
    { .scan_code = 23, .event_type = 0, .ascii = 'i' },
    { .scan_code = 151, .event_type = 1, .ascii = 'i' },
    { .scan_code = 24, .event_type = 0, .ascii = 'o' },
    { .scan_code = 152, .event_type = 1, .ascii = 'o' },
    { .scan_code = 25, .event_type = 0, .ascii = 'p' },
    { .scan_code = 153, .event_type = 1, .ascii = 'p' },
    { .scan_code = 26, .event_type = 0, .ascii = '[' },
    { .scan_code = 154, .event_type = 1, .ascii = '[' },
    { .scan_code = 27, .event_type = 0, .ascii = ']' },
    { .scan_code = 155, .event_type = 1, .ascii = ']' },

    // A row.
    { .scan_code = 30, .event_type = 0, .ascii = 'a' },
    { .scan_code = 158, .event_type = 1, .ascii = 'a' },
    { .scan_code = 31, .event_type = 0, .ascii = 's' },
    { .scan_code = 159, .event_type = 1, .ascii = 's' },
    { .scan_code = 32, .event_type = 0, .ascii = 'd' },
    { .scan_code = 160, .event_type = 1, .ascii = 'd' },
    { .scan_code = 33, .event_type = 0, .ascii = 'f' },
    { .scan_code = 161, .event_type = 1, .ascii = 'f' },
    { .scan_code = 34, .event_type = 0, .ascii = 'g' },
    { .scan_code = 162, .event_type = 1, .ascii = 'g' },
    { .scan_code = 35, .event_type = 0, .ascii = 'h' },
    { .scan_code = 163, .event_type = 1, .ascii = 'h' },
    { .scan_code = 36, .event_type = 0, .ascii = 'j' },
    { .scan_code = 164, .event_type = 1, .ascii = 'j' },
    { .scan_code = 37, .event_type = 0, .ascii = 'k' },
    { .scan_code = 165, .event_type = 1, .ascii = 'k' },
    { .scan_code = 38, .event_type = 0, .ascii = 'l' },
    { .scan_code = 166, .event_type = 1, .ascii = 'l' },
    { .scan_code = 39, .event_type = 0, .ascii = ';' },
    { .scan_code = 167, .event_type = 1, .ascii = ';' },
    { .scan_code = 40, .event_type = 0, .ascii = '\'' },
    { .scan_code = 168, .event_type = 1, .ascii = '\'' },

    // Symbols.
    { .scan_code = 43, .event_type = 0, .ascii = '#' },
    { .scan_code = 171, .event_type = 1, .ascii = '#' },
    { .scan_code = 57, .event_type = 0, .ascii = ' ' },
    { .scan_code = 185, .event_type = 1, .ascii = ' ' },
    { .scan_code = 53, .event_type = 0, .ascii = '/' },
    { .scan_code = 181, .event_type = 1, .ascii = '/' },
    { .scan_code = 41, .event_type = 0, .ascii = '`' },
    { .scan_code = 169, .event_type = 1, .ascii = '`' },
    
    // Z row.
    { .scan_code = 44, .event_type = 0, .ascii = 'z' },
    { .scan_code = 172, .event_type = 1, .ascii = 'z' },
    { .scan_code = 45, .event_type = 0, .ascii = 'x' },
    { .scan_code = 173, .event_type = 1, .ascii = 'x' },
    { .scan_code = 46, .event_type = 0, .ascii = 'c' },
    { .scan_code = 174, .event_type = 1, .ascii = 'c' },
    { .scan_code = 47, .event_type = 0, .ascii = 'v' },
    { .scan_code = 175, .event_type = 1, .ascii = 'v' },
    { .scan_code = 48, .event_type = 0, .ascii = 'b' },
    { .scan_code = 176, .event_type = 1, .ascii = 'b' },
    { .scan_code = 49, .event_type = 0, .ascii = 'n' },
    { .scan_code = 177, .event_type = 1, .ascii = 'n' },
    { .scan_code = 50, .event_type = 0, .ascii = 'm' },
    { .scan_code = 178, .event_type = 1, .ascii = 'm' },
    { .scan_code = 51, .event_type = 0, .ascii = ',' },
    { .scan_code = 179, .event_type = 1, .ascii = ',' },
    { .scan_code = 52, .event_type = 0, .ascii = '.' },
    { .scan_code = 180, .event_type = 1, .ascii = '.' },

    // Numpad.
    { .scan_code = 71, .event_type = 0, .ascii = '7' },
    { .scan_code = 199, .event_type = 1, .ascii = '7' },
    { .scan_code = 72, .event_type = 0, .ascii = '8' },
    { .scan_code = 200, .event_type = 1, .ascii = '8' },
    { .scan_code = 73, .event_type = 0, .ascii = '9' },
    { .scan_code = 201, .event_type = 1, .ascii = '9' },
    { .scan_code = 75, .event_type = 0, .ascii = '4' },
    { .scan_code = 203, .event_type = 1, .ascii = '4' },
    { .scan_code = 76, .event_type = 0, .ascii = '5' },
    { .scan_code = 204, .event_type = 1, .ascii = '5' },
    { .scan_code = 77, .event_type = 0, .ascii = '6' },
    { .scan_code = 205, .event_type = 1, .ascii = '6' },
    { .scan_code = 79, .event_type = 0, .ascii = '1' },
    { .scan_code = 207, .event_type = 1, .ascii = '1' },
    { .scan_code = 80, .event_type = 0, .ascii = '2' },
    { .scan_code = 208, .event_type = 1, .ascii = '2' },
    { .scan_code = 81, .event_type = 0, .ascii = '3' },
    { .scan_code = 209, .event_type = 1, .ascii = '3' },
    { .scan_code = 82, .event_type = 0, .ascii = '0' },
    { .scan_code = 210, .event_type = 1, .ascii = '0' },
    { .scan_code = 83, .event_type = 0, .ascii = '.' },
    { .scan_code = 211, .event_type = 1, .ascii = '.' },
    { .scan_code = 224, .event_type = 1, .ascii = '/' },        // Numpad key up shares key down code.
    { .scan_code = 55, .event_type = 0, .ascii = '*' },
    { .scan_code = 183, .event_type = 1, .ascii = '*' },
    { .scan_code = 74, .event_type = 0, .ascii = '-' },
    { .scan_code = 202, .event_type = 1, .ascii = '-' },
    { .scan_code = 78, .event_type = 0, .ascii = '+' },
    { .scan_code = 206, .event_type = 1, .ascii = '+' },
    { .scan_code = 69, .event_type = 0, .ascii = 0 },
    { .scan_code = 197, .event_type = 1, .ascii = 0 },

    // Control keys.
    { .scan_code = 28, .event_type = 0, .ascii = 0 },   // Enter
    { .scan_code = 156, .event_type = 1, .ascii = 0 },
    { .scan_code = 14, .event_type = 0, .ascii = 0 },   // Backspace
    { .scan_code = 142, .event_type = 1, .ascii = 0 },
    { .scan_code = 42, .event_type = 0, .ascii = 0 },   // Left Shift
    { .scan_code = 170, .event_type = 1, .ascii = 0 },
    { .scan_code = 54, .event_type = 0, .ascii = 0 },   // Right Shift
    { .scan_code = 182, .event_type = 1, .ascii = 0 },
    { .scan_code = 29, .event_type = 0, .ascii = 0 },   // Left Ctrl
    { .scan_code = 157, .event_type = 1, .ascii = 0 },
    { .scan_code = 56, .event_type = 0, .ascii = 0 },   // Left Alt
    { .scan_code = 184, .event_type = 1, .ascii = 0 },
    { .scan_code = 15, .event_type = 0, .ascii = 0 },   // Tab
    { .scan_code = 143, .event_type = 1, .ascii = 0 },
    { .scan_code = 1, .event_type = 0, .ascii = 0 },    // Escape
    { .scan_code = 129, .event_type = 1, .ascii = 0 },
    { .scan_code = 58, .event_type = 0, .ascii = 0 },   // Caps-lock
    { .scan_code = 186, .event_type = 1, .ascii = 0 },
};

// Scancodes for key-down and key-up to ASCII value.
KeyEvent_t* scancode_map[256] = {0};

uint8_t ps2_read()
{
    // Wait for the Status Register to be not busy for reading.
    while ((inb(PORT_PS2_STATUSCMD) & 1) == 0);

    return inb(0x60);
}

/*
 * Read directly from the IO port without checking the status register.
 * Used when handling IRQ1 interrupts.
*/
uint8_t ps2_read_no_wait()
{
    return inb(0x60);
}

void ps2_write(uint16_t port, uint8_t value)
{
    // Wait for the Status Register to be not busy for writing.
    while ((inb(PORT_PS2_STATUSCMD) & 2) != 0);

    outb(port, value);
}

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

    // Flush data.
    ps2_read_no_wait();

    // Enable interrupt and scan-code translation config.
    uint8_t config = ps2_read_config();
    config |= (1 << 0) | (1 << 6);

    // Enable mouse interrupt if its present.
    if ((config & (1 << 5)) != 0) {
       config |= (1 << 1); 
    }

    // Write back the confing.
    ps2_write_config(config);

    // Enable the keyboard.
    ps2_write(PORT_PS2_STATUSCMD, 0xAE);

    // Enable the mouse if present.
    if ((config & (1 << 5)) != 0) {
        ps2_write(PORT_PS2_STATUSCMD, 0xA8);
    }

    // Setup the IRQ1 redirect in the I/O APIC to come to our keyboard vector in the IDT.
    ioapic_redirect_irq(bsp_lapic_id, KEYBOARD_VECTOR, 1, true);

    // Setup the scancode mapping.
    for (uint64_t i = 0; i < sizeof(key_map) / sizeof(KeyEvent_t); i++) {
        KeyEvent_t *pKE = &key_map[i];
        if (scancode_map[pKE->scan_code] != NULL) {
            kprintf("Map Error: Scan code %d already registered.\n", pKE->scan_code);
        }
        scancode_map[pKE->scan_code] = pKE;
    }

    kprintf("PS2 initialized.\n");
}
