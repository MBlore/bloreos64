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
#ifndef _BLOREOS_ACPI_H
#define _BLOREOS_ACPI_H

#include <stdint.h>

void acpi_init();

void _parse_madt(struct sysdesc * desc);

// I/O APIC Structure inside the ICL of the MADT (ID 1).
struct ioapic {
    char type;
    char length;
    char ioapic_id;
    char reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

// Interrupt Source Override table in the MADT (ID 2).
struct iso {
    char type;
    char length;
    uint8_t bus_source;
    uint8_t irq_source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

#define IOAPIC_LIST_LEN 32
#define ISO_LIST_LEN 128
extern struct ioapic *ioapic_list[];
extern struct iso *iso_list[];

#endif