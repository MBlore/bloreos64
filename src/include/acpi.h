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

// I/O APIC Structure inside the ICL of the MADT (ID 1).
struct ioapic
{
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

// System Description Table Header (DESCRIPTION_HEADER)
struct sysdesc {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct address_structure
{
    uint8_t address_space_id;    // 0 - system memory, 1 - system I/O
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t reserved;
    uint64_t address;
} __attribute__((packed));

// HPET table (High Precision Event Timer).
struct hpet
{
    struct sysdesc desc;
    uint8_t hardware_rev_id;
    uint8_t comparator_count:5;
    uint8_t counter_size:1;
    uint8_t reserved:1;
    uint8_t legacy_replacement:1;
    uint16_t pci_vendor_id;
    struct address_structure address;
    uint8_t hpet_number;
    uint16_t minimum_tick;
    uint8_t page_protection;
} __attribute__((packed));

// MCFG table
struct mcfg_entry {
    struct sysdesc desc;
    char reserved1[8];
    uint64_t mmio_base;
    uint16_t segment;
    uint8_t start;
    uint8_t end;
    uint32_t reserved2;
} __attribute__((packed));

#define IOAPIC_LIST_LEN 32
#define ISO_LIST_LEN 128

extern struct ioapic *ioapic_list[];
extern struct iso *iso_list[];
extern struct hpet *hpet;
extern struct mcfg_entry *mcfg;

#endif