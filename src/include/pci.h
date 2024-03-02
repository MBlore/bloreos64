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
#ifndef _BLOREOS_PCI_H
#define _BLOREOS_PCI_H

#include <stdint.h>

// PCI Configuration register offset values.
#define PCI_REG0_OFFSET 0x0     // Device ID, VendorID
#define PCI_REG1_OFFSET 0x4     // Status, Command
#define PCI_REG2_OFFSET 0x8     // Class Code, Subclass, Prog IF, Revision ID
#define PCI_REG3_OFFSET 0xC     // BIST, Header Type, Latency Timer, Cache Line Size
#define PCI_REG4_OFFSET 0x10    // BAR0 Base Address
#define PCI_REG5_OFFSET 0x14    // BAR1 Base Address

struct pci_device {
    uint8_t class_code;
    uint8_t sub_class_code;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t prog_if;
    const char* description;
    uint64_t address;
    uint32_t header_type;
};

extern struct pci_device *pci_devices[];
extern uint8_t pci_device_cnt;

void pci_init();
struct pci_device* pci_find_device(uint8_t class, uint8_t subclass);

uint32_t pci_device_read(struct pci_device *dev, uint32_t offset, uint8_t size);
void pci_device_write(struct pci_device *dev, uint32_t offset, uint8_t size, uint32_t val);

#endif