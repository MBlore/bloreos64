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

#define IOAPIC_REDIR_MASK_BIT (1U << 16)

/*
 * ACPI MADT Interrupt Source Override flags:
 *
 * Bits 0-1: Polarity
 *   00 = Conforms to bus
 *   01 = Active high
 *   11 = Active low
 *
 * Bits 2-3: Trigger mode
 *   00 = Conforms to bus
 *   01 = Edge
 *   11 = Level
 *
 * Value 10 is reserved in both fields.
 *
 * For ISA interrupts, conforming means active-high,
 * edge-triggered.
 */
#define ACPI_INT_POLARITY_MASK  0x3U
#define ACPI_INT_TRIGGER_SHIFT  2U
#define ACPI_INT_TRIGGER_MASK   0x3U
#define ACPI_INT_RESERVED       2U

#define ACPI_INT_CONFORMS       0U
#define ACPI_INT_ACTIVE_HIGH    1U
#define ACPI_INT_ACTIVE_LOW     3U
#define ACPI_INT_EDGE           1U
#define ACPI_INT_LEVEL          3U

#define IOAPIC_REDIR_ACTIVE_LOW (1ULL << 13)
#define IOAPIC_REDIR_LEVEL      (1ULL << 15)

/*
 * I/O APIC redirection table starts at register 0x10.
 * Each interrupt uses two consecutive 32-bit registers.
 */
#define IOAPIC_REDIR_BASE_REG       0x10U
#define IOAPIC_REDIR_REGS_PER_ENTRY 2U

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
 * Returns the number of interrupt redirection entries supported
 * by this I/O APIC.
 *
 * IOAPICVER bits 16-23 contain the maximum zero-based
 * redirection entry index, so add one.
 */
static uint32_t ioapic_redirection_count(struct ioapic *apic)
{
    uint32_t version = ioapic_read(apic, IOAPICVER);
    return ((version >> 16) & 0xFFU) + 1U;
}

/*
 * Find the I/O APIC responsible for a Global System Interrupt.
*/
struct ioapic* _get_ioapic_from_gsi(uint32_t gsi)
{
    for (int i = 0; i < IOAPIC_LIST_LEN; i++) {
        struct ioapic *apic = ioapic_list[i];

        // End of list.
        if (apic == NULL)
            return NULL;

        uint32_t count = ioapic_redirection_count(apic);
            
        if (gsi >= apic->gsi_base && (gsi - apic->gsi_base) < count) {
            return apic;
        }
    }

    return NULL;
}

/*
 * 
*/
bool ioapic_redirect_gsi(uint32_t lapic_id, uint8_t vector, uint32_t gsi, uint16_t flags, bool status)
{
    struct ioapic *apic = _get_ioapic_from_gsi(gsi);
    if (apic == NULL) {
        kprintf("IOAPIC: No controller handles GSI %u\n", gsi);
        return false;
    }

    uint64_t redirect = vector;

    /*
     * Decode ACPI interrupt flags.
     *
     * Bits 0-1: Interrupt polarity
     *   00 = Conforms to bus (ISA defaults to active high)
     *   01 = Active high
     *   10 = Reserved (invalid)
     *   11 = Active low
     *
     * Bits 2-3: Trigger mode
     *   00 = Conforms to bus (ISA defaults to edge-triggered)
     *   01 = Edge-triggered
     *   10 = Reserved (invalid)
     *   11 = Level-triggered
     */
    uint32_t polarity = flags & ACPI_INT_POLARITY_MASK;
    uint32_t trigger = (flags >> ACPI_INT_TRIGGER_SHIFT) & ACPI_INT_TRIGGER_MASK;

    // Reject reserved ACPI flag values.
    if (polarity == ACPI_INT_RESERVED || trigger == ACPI_INT_RESERVED) {
        kprintf("IOAPIC: Invalid interrupt flags\n");
        return false;
    }

    // IOAPIC redirection bit 13: 1 = active low.
    if (polarity == ACPI_INT_ACTIVE_LOW) {
        redirect |= IOAPIC_REDIR_ACTIVE_LOW;
    }

    // IOAPIC redirection bit 15: 1 = level-triggered.
    if (trigger == ACPI_INT_LEVEL) {
        redirect |= IOAPIC_REDIR_LEVEL;
    }

    // The enabled mask is on the 16th bit.
    if (!status) {
        redirect |= IOAPIC_REDIR_MASK_BIT;
    }

    // Put the APIC ID in to the upper 8 bits - this selects which CPU handles the interrupt.
    if (lapic_id > UINT8_MAX) {
        kprintf("IOAPIC: Unsupported LAPIC ID %u\n", lapic_id);
        return false;
    }

    redirect |= (uint64_t)lapic_id << 56;

    // Get the location inside the redirect table for the GSI.
    uint32_t io_redirect_table_reg = (gsi - apic->gsi_base) * IOAPIC_REDIR_REGS_PER_ENTRY + IOAPIC_REDIR_BASE_REG;
    if (io_redirect_table_reg > UINT8_MAX - 1U) {
        kprintf("IOAPIC: Invalid redirection register index\n");
        return false;
    }

    /*
     * Configure the I/O APIC redirection entry safely.
     *
     * Mask the interrupt before changing its destination or
     * configuration to avoid delivering an interrupt while
     * the entry is only partially updated.
    */
    uint32_t redirect_low = (uint32_t)redirect;
    uint32_t redirect_high = (uint32_t)(redirect >> 32);

    // Mask the existing redirection entry before modifying it.
    uint32_t current_low = ioapic_read(apic, io_redirect_table_reg);
    ioapic_write(
        apic,
        io_redirect_table_reg,
        current_low | IOAPIC_REDIR_MASK_BIT
    );

    // Configure the destination LAPIC.
    ioapic_write(apic, io_redirect_table_reg + 1, redirect_high);

    // Apply the final configuration, including the desired mask state.
    ioapic_write(apic, io_redirect_table_reg, redirect_low);

    kprintf("Applied I/O APIC redirect: Vector %d, GSI %d.\n", vector, gsi);

    return true;
}

/*
 * Apply an override to the specified IRQ taking in to account the Interrupt Service Overrides
 * contained in the specified APIC.
*/
bool ioapic_redirect_irq(uint32_t lapic_id, uint8_t vector, uint8_t irq, bool status)
{
    for (int i = 0; i < ISO_LIST_LEN; i++) {
        struct iso* pISO = iso_list[i];
        if (pISO == NULL) {
            // Reach end of list.
            break;
        }

        // Does this override entry affect the IRQ we want to redirect?
        if (irq == pISO->irq_source) {
            kprintf("I/O APIC using override from %d to %d.\n", irq, pISO->gsi);
            return ioapic_redirect_gsi(lapic_id, vector, pISO->gsi, pISO->flags, status);
        }
    }

    // No overrides.
    return ioapic_redirect_gsi(lapic_id, vector, irq, 0, status);
}

/*
 * Returns true if an I/O APIC exists that can route
 * the specified Global System Interrupt.
 *
 * This does not mean the GSI is unallocated.
*/
bool ioapic_supports_gsi(uint32_t gsi)
{
    return _get_ioapic_from_gsi(gsi) != NULL;
}



