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
    The ACPI is a nest of tables in memory.
*/
#include <acpi.h>
#include <mem.h>
#include <cpu.h>
#include <str.h>

// Interrupt Controller Structure Types 
#define ICS_ID_CPU_LAPIC 0
#define ICS_ID_IO_APIC 1

struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST,
    .revision = 0};

// Root System Description Pointer Structure
struct rsdp {
    char signature[8];              // "RSD PTR "
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t ext_checksum;
    char reserved[3];
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
    uint32_t entries[];             // 32-bit (RSDT) or 64-bit entries (XSDT).
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
struct madt {
    struct sysdesc desc;
    uint32_t lapic_addr;        // The local APIC address which should match what we get from the MSR, which should be 0xFEE00000.
    uint32_t flags;
} __attribute__((packed));

// I/O APIC Structure inside the ICL of the MADT.
struct ioapic {
    char type;
    char length;
    char ioapic_id;
    char reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

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

    // 36 bytes in fields up to entries, 4 byte per entry.
    uint32_t items = (rsdt->length - 36) / 4;
    kprintf("RSDT Entries Count: %d\n", items);

    kprintf("RSDT Addr: 0x%X\n", rsdt);

    // Walk the table.
    for (uint32_t i = 0; i < items; i++) {
        // Report entry.
        struct sysdesc *desc = (struct sysdesc*)((uint64_t)rsdt->entries[i] + vmm_higher_half_offset);
        kprintf("Entry %d Sig: %c%c%c%c\n", i, desc->signature[0], desc->signature[1], desc->signature[2], desc->signature[3]);
        kprintf("Entry %d Length: %d\n", i, desc->length);

        if (desc->signature[0] == 'A' &&
            desc->signature[1] == 'P' &&
            desc->signature[2] == 'I' &&
            desc->signature[3] == 'C') {
                struct madt *pMadt = (struct madt*)desc;
                kprintf("Local IC Addr: 0x%X\n", pMadt->lapic_addr);

                
                kprintf("Start MADT: 0x%X\n", pMadt);
                kprintf("MADT Length: %d\n", (int)pMadt->desc.length);

                // Get a pointer to the begining of the interrupt
                // control structure list which is located at the end of the MADT.
                char *pICLStart = (char*)pMadt + sizeof(struct madt);
                char *pICLItem = pICLStart;

                // Walk the ICL list.
                do {
                    kprintf("ICL Entry ID: %d\n", (int)*pICLItem);
                    kprintf("ICL Entry Len: %d\n", (int)pICLItem[1]);

                    // Check the type, we're looking for the I/O APIC.
                    if (*pICLItem == ICS_ID_IO_APIC) {
                        // There maybe multiple I/O apic entries, each handling
                        // different sets of IRQs.
                        kprintf("Found an IO/APIC entry.\n");
                        struct ioapic *pIOApic = (struct ioapic*)pICLItem;

                        kprintf("IO APIC ID: %d\n", (int)pIOApic->ioapic_id);
                        kprintf("IO APIC Addr: 0x%X\n", pIOApic->ioapic_addr);
                        kprintf("IO APIC GSI Base: %d\n", pIOApic->gsi_base);
                    }

                    // Move past this entire structure (length is at 2nd byte).
                    pICLItem += pICLItem[1];
                } while (pICLItem - pICLStart < pMadt->desc.length);
            }
    }
}

