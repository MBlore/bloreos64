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

#include <stdint.h>
#include <pci.h>
#include <nvme.h>
#include <str.h>
#include <kernel.h>

void nvme_init()
{
    struct pci_device* dev = pci_find_device(0x01, 0x08);
    if (dev == NULL) {
        kprintf("NVME: Controller not found.\n");
        return;
    }

    kprintf("NVME Header Type: 0x%X\n", dev->header_type);

    // To determine the address space size for a PCI device, you must
    // save the original value of the BAR, write a value of all 1's to the register,
    // then read it back, then restore the original value back.
    // Reg4 == BAR0.
    uint32_t bar0 = pci_device_read(dev, PCI_REG4_OFFSET, 4);
    pci_device_write(dev, PCI_REG4_OFFSET, 4, ~0);
    uint32_t size_low = pci_device_read(dev, PCI_REG4_OFFSET, 4);
    pci_device_write(dev, PCI_REG4_OFFSET, 4, bar0);

    // The first bit on the bar address data tells us if this BAR is a
    // memory space or an I/O bar type.

    // For NVMe, the BAR0 should be a memory space bar type.
    if (bar0 & 1) {
        // I/O Space BAR
        kprintf("**FATAL**: BAR0 in the NVME controller should be memory space.");
    } else {
        // Memory Space BAR
        kprintf("NVME: Memory Space BAR0 verified.\n");
    }

    // Bits 1-2 in the BAR address, tells us how wide in size the BAR is.
    // Let's check if the BAR is 64-bit addressed which is what we expect.
    // Bits 1-2 will have the value 2 when the BAR is 64-bit.
    if ((bar0 & 0b111) == 0b100) {
        kprintf("BAR0 is 64-bit.\n");
    }
    
    // The high address value of the BAR is contained in the next 4 bytes.
    uint32_t base_high = pci_device_read(dev, PCI_REG4_OFFSET + 4, 4);
    kprintf("NVME Base High: 0x%X\n", base_high);
    
    // The low part of the address is in bits 4-31 of BAR0.
    uint32_t base_low = bar0 & 0xfffffff0;

    // Now we can combine for the 64-bit base address.
    uint64_t bar0_base_addr = base_low;
    bar0_base_addr |= ((uint64_t)base_high << 32);

    // The amount of memory can then be determined by masking the information bits (1-4),
    // and incrementing the value by 1.
    size_t length = ~(size_low & ~0b1111) + 1;
    kprintf("BAR0 Length: %lu\n", length);

    // The NVMe BAR0 registers are located at the bar0_base_addr address.
    

    
    kprintf("NVME: Initialized.\n");
}