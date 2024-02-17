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

#include <ioapic.h>
#include <acpi.h>
#include <mem.h>
#include <str.h>
#include <stdbool.h>

/*
 * Write to a memory mapped register in the I/O APIC. The write is a 2 phase write.
*/
void ioapic_write(struct ioapic *pApic, const uint8_t offset, const uint32_t val)
{
    uint64_t base = (uint64_t)pApic->ioapic_addr + vmm_higher_half_offset;

    // Tell IOREGSEL where we want to write to
    *(volatile uint32_t*)base = offset;

    // Write the value to IOWIN
    *(volatile uint32_t*)(base + 0x10) = val;
}

/*
 * Read from a memory mapped register in the I/O APIC. The read is a 2 phase read.
*/
uint32_t ioapic_read(struct ioapic *pApic, const uint8_t offset)
{
    uint64_t base = (uint64_t)pApic->ioapic_addr + vmm_higher_half_offset;

    // Tell IOREGSEL where we want to read from
    *(volatile uint32_t*)(base) = offset;

    // Return the data from IOWIN
    return *(volatile uint32_t*)(base + 0x10);
}

/*
 * Gets the maximum GSI value for the specified I/O APIC.
*/
static uint64_t ioapic_max_gsi(struct ioapic *pIOApic)
{
    uint64_t max = (ioapic_read(pIOApic, IOAPICVER) & 0xFF0000) >> 16;
    return max;
}

/*
 * Get the specific I/O APIC that manages the specified GSI.
*/
struct ioapic* _get_ioapic_from_gsi(uint32_t gsi)
{
    for (int i = 0; i < IOAPIC_LIST_LEN; i++) {
        struct ioapic *pIOApic = ioapic_list[i];

        // End of list.
        if (pIOApic == NULL)
            return NULL;
            
        if (gsi >= pIOApic->gsi_base && gsi < pIOApic->gsi_base + ioapic_max_gsi(pIOApic)) {
            return pIOApic;
        }
    }

    return NULL;
}

/*
 * 
*/
void _ioapic_redirect_gsi(uint32_t lapic_id, uint8_t vector, uint8_t gsi, uint16_t flags, bool status)
{
    struct ioapic *pIOApic = _get_ioapic_from_gsi(gsi);

    uint64_t redirect = vector;

    // Set the interrupt polarity to active low.
    if ((flags & (1 << 1)) != 0) {
        redirect |= (1 << 13);
    }

    // Set interrupt to be level-triggered (not edge-triggered).
    if ((flags & (1 << 3)) != 0) {
        redirect |= (1 << 15);
    }

    // The enabled mask is on the 16th bit.
    if (!status) {
        redirect |= (1 << 16);
    }

    // Put the APIC ID in to the upper 8 bits - this selects which CPU handles the interrupt.
    redirect |= (uint64_t)lapic_id << 56;

    // Get the location inside the redirect table for the GSI.
    uint32_t io_redirect_table_reg = (gsi - pIOApic->gsi_base) * 2 + 16;

    // The high and low of the table entry get written in 2 seperate write commands.
    ioapic_write(pIOApic, io_redirect_table_reg, (uint32_t)redirect);
    ioapic_write(pIOApic, io_redirect_table_reg + 1, (uint32_t)(redirect >> 32));

    kprintf("Applied I/O APIC redirect: Vector %d, GSI %d.\n", vector, gsi);
}

/*
 * Apply an override to the specified IRQ taking in to account the Interrupt Service Overrides
 * contained in the specified APIC.
*/
void ioapic_redirect_irq(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool status)
{
    for (int i = 0; i < ISO_LIST_LEN; i++) {
        struct iso* pISO = iso_list[i];
        if (pISO == NULL) {
            // Reach end of list.
            break;
        }

        // Does this override entry affect the IRQ we want to redirect?
        if (irq == pISO->irq_source) {
            _ioapic_redirect_gsi(lapic_id, vector, pISO->gsi, pISO->flags, status);
            return;
        }
    }

    // No overrides.
    _ioapic_redirect_gsi(lapic_id, vector, irq, 0, status);
}



