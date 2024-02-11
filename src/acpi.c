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
    Everything to manage the ACPI.
*/
#include <acpi.h>
#include <mem.h>
#include <cpu.h>
#include <str.h>

struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0};

// Root System Description Pointer Structure
struct rsdp {
    char signature[8];          // "RSD PTR "
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t ext_checksum;
    char reserved[3];
};

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

// Multiple APIC Description Table (MADT)
struct sysdesc_madt {
    struct sysdesc desc;
    uint32_t lic_addr;
    uint32_t flags;
    // 2 bytes / type/length
};

// Root System Description Table
struct rsdt {
    char signature[4];              // "RSDT"
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    uint64_t oem_table_id;
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
    uint32_t entries;              // 32-bit (RSDT) or 64-bit entries (XSDT).
};

struct rsdp *rsdp = NULL;
struct rsdt *rsdt = NULL;

void acpi_init()
{
    kprintf("Initialzing ACPI...\n");

    rsdp = rsdp_request.response->address;
    kprintf("RSDP Revision: %d\n", rsdp->revision);

    if (rsdp->revision >= 2) {
        kprintf("XSDT is available.\n");
        rsdt = (struct rsdt*)((uint64_t)rsdp->xsdt_addr + vmm_higher_half_offset);
    } else {
        kprintf("XSDT is not available.\n");
        rsdt = (struct rsdt*)((uint64_t)rsdp->rsdt_addr + vmm_higher_half_offset);
    }

    kprintf("RSDT Sig: %c%c%c%c\n", rsdt->signature[0], rsdt->signature[1], rsdt->signature[2], rsdt->signature[3]);
    kprintf("RSDT Length: %d\n", rsdt->length);

    uint32_t items = (rsdt->length - 36) / 4; // 36 bytes in fields up to entries, 4 byte per entry.
    kprintf("RSDT Entries Count: %d\n", items);

    kprintf("RSDT Addr: 0x%X\n", rsdt);

    // Walk the table.
    uint32_t *pEntry = &rsdt->entries;
    for (uint32_t i = 0; i < items; i++) {
        // Report entry.        
        struct sysdesc *desc = (struct sysdesc*)((uint64_t)*pEntry + vmm_higher_half_offset);
        kprintf("Entry %d Sig: %c%c%c%c\n", i, desc->signature[0], desc->signature[1], desc->signature[2], desc->signature[3]);
        kprintf("Entry %d Length: %d\n", i, desc->length);

        pEntry++;
    }
}

