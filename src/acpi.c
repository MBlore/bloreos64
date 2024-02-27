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
    Everything to parse, read, and write the ACPI.
    The ACPI is a nest of tables in memory. It uses some parts of memory for memory mapped registers.
    https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/
*/
#include <acpi.h>
#include <mem.h>
#include <cpu.h>
#include <str.h>
#include <mem.h>
#include <stdbool.h>

// Interrupt Controller Structure Types 
#define ICS_ID_IO_APIC 1
#define ICS_ID_ISO 2

// Signature strings
#define SDT_APIC_SIG    "APIC"
#define SDT_APIC_HPET   "HPET"

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
} __attribute__((packed));

// Root System Description Table (32-bit)
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
    uint32_t entries[];             // 32-bit entries.
} __attribute__((packed));

// Root System Description Table (64-bit)
struct xsdt {
    char signature[4];              // "RSDT"
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    uint64_t oem_table_id;
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
    uint64_t entries[];             // 64-bit entries.
} __attribute__((packed));

// Multiple APIC Description Table (MADT)
struct madt {
    struct sysdesc desc;
    uint32_t lapic_addr;        // The local APIC address which should match what we get from the MSR, which should be 0xFEE00000.
    uint32_t flags;
} __attribute__((packed));

struct rsdp *rsdp = NULL;
struct rsdt *rsdt = NULL;
struct madt *pMadt = NULL;
struct hpet *hpet = NULL;
uint32_t rsdt_entry_count;

/*
 * We don't have a proper malloc() yet so we are using fixed sized arrays to hold pointers to the structs in the MADT.
*/
struct ioapic *ioapic_list[IOAPIC_LIST_LEN] = {0};
struct iso *iso_list[ISO_LIST_LEN] = {0};

void add_ioapic(struct ioapic *pIOApic)
{
    for (int i = 0; i < IOAPIC_LIST_LEN; i++) {
        if (ioapic_list[i] == NULL) {
            ioapic_list[i] = pIOApic;
            return;
        }
    }
}

void add_iso(struct iso *pIso)
{
    for (int i = 0; i < ISO_LIST_LEN; i++) {
        if (iso_list[i] == NULL) {
            iso_list[i] = pIso;
            kprintf("ISO Override: IRQ %d -> GSI %d\n", pIso->irq_source, pIso->gsi);
            return;
        }
    }
}

void _parse_madt(struct sysdesc *desc)
{
    // Found the APIC entry. Parse this MADT structure to find the I/O APIC address.
    pMadt = (struct madt *)desc;

    kprintf("Start MADT: 0x%X\n", pMadt);
    kprintf("MADT Length: %d\n", (int)pMadt->desc.length);

    // Get a pointer to the beginning of the interrupt
    // control structure list which is located at the end of the MADT.
    char *pICLStart = (char *)pMadt + sizeof(struct madt);
    char *pICLItem = pICLStart;

    // Walk the ICL list.
    do
    {
        // Check the type, we're looking for the I/O APIC.
        if (*pICLItem == ICS_ID_IO_APIC)
        {
            struct ioapic *pIOApic = (struct ioapic *)pICLItem;
            add_ioapic(pIOApic);

            kprintf("IO APIC ID: %d\n", (int)pIOApic->ioapic_id);
            kprintf("IO APIC Addr: 0x%X\n", pIOApic->ioapic_addr);
            kprintf("IO APIC GSI Base: %d\n", pIOApic->gsi_base);
        }
        else if (*pICLItem == ICS_ID_ISO)
        {
            struct iso *pIso = (struct iso *)pICLItem;
            add_iso(pIso);
        }

        // Move past this entire structure (length is at 2nd byte).
        pICLItem += pICLItem[1];
    } while (pICLItem - pICLStart < pMadt->desc.length);
}

/*
 * The ACPI init will look for the I/O APIC address to later setup IRQ redirections.
 * We start at the RSDP pointer from Limine, which leads us to the table RSDT.
 * The RSDT has entries to various info structures. We look for the MADT in these entries
 * which contains a list of more structures. Finally, in this list of structures we find
 * the I/O APIC structure with the address of it.
*/
void acpi_init()
{
    bool xsdt_enabled = false;

    kprintf("Initialzing ACPI...\n");

    rsdp = rsdp_request.response->address;
    kprintf("RSDP Revision: %d\n", rsdp->revision);

    if (rsdp->revision >= 2) {
        xsdt_enabled = true;
        kprintf("XSDT is available but not yet supported.\n");
        asm("hlt");
        __builtin_unreachable();
        //rsdt = (struct xsdt*)((uint64_t)rsdp->xsdt_addr + vmm_higher_half_offset);
    } else {
        kprintf("XSDT is not available.\n");
        rsdt = (struct rsdt*)((uint64_t)rsdp->rsdt_addr + vmm_higher_half_offset);
    }

    // 36 bytes in fields up to entries, x bytes per entry.
    rsdt_entry_count = (rsdt->length - 36) / (xsdt_enabled ? 8 : 4);

    // Walk the RSDT entries, looking for the APIC entry.
    for (uint32_t i = 0; i < rsdt_entry_count; i++) {
        // Report entry.
        struct sysdesc *desc = (struct sysdesc*)((uint64_t)rsdt->entries[i] + vmm_higher_half_offset);

        if (memcmp(desc->signature, SDT_APIC_HPET, 4) == 0) {
            hpet = (struct hpet*)desc;
            continue;
        }

        if (memcmp(desc->signature, SDT_APIC_SIG, 4) == 0) {
            _parse_madt(desc);
            continue;
        }

        kprintf("Found ACPI Table: %c%c%c%c\n", desc->signature[0], desc->signature[1], desc->signature[2], desc->signature[3]);
    }

    kprintf("ACPI initialized.\n");
}