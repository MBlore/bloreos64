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

/*
 * Write to a memory mapped register in the I/O APIC. The write is a 2 phase write.
*/
/*
static void ioapic_write(struct ioapic *pApic, const uint8_t offset, const uint32_t val)
{
    uint64_t base = (uint64_t)pApic->ioapic_addr + vmm_higher_half_offset;

    // Tell IOREGSEL where we want to write to
    *(volatile uint32_t*)base = offset;

    // Write the value to IOWIN
    *(volatile uint32_t*)(base + 0x10) = val;
}
*/

/*
 * Read from a memory mapped register in the I/O APIC. The read is a 2 phase read.
*/
static uint32_t ioapic_read(struct ioapic *pApic, const uint8_t offset)
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
    return (ioapic_read(pIOApic, IOAPICVER) & 0xFF0000) >> 16;
}

/*
 * Get the specific I/O APIC that manages the specified GSI.
*/
struct ioapic* get_ioapic_from_gsi(uint32_t gsi)
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