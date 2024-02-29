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

struct pci_device {
    uint8_t class_code;
    uint8_t sub_class_code;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t prog_if;
    const char* description;
    uint64_t address;
    uint64_t bar0_address;
    uint64_t bar1_address;
    uint32_t header_type;
};

extern struct pci_device *pci_devices[];
extern uint8_t pci_device_cnt;

void pci_init();
struct pci_device* pci_find_device(uint8_t class, uint8_t subclass);

uint64_t pci_get_bar0_address();
uint64_t pci_get_bar1_address();

#endif