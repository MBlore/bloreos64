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
#include <vm.h>
#include <mem.h>

#define NVME_PCI_CLASS     0x01
#define NVME_PCI_SUBCLASS  0x08
#define NVME_PCI_PROG_IF   0x02

#define PCI_BAR_IO_SPACE        (1U << 0)
#define PCI_BAR_TYPE_MASK       (3U << 1)
#define PCI_BAR_TYPE_64BIT      (2U << 1)
#define PCI_BAR_ADDRESS_MASK    0xFFFFFFF0U

#define NVME_REG_VS 0x08

#define NVME_VS_MAJOR_SHIFT     16
#define NVME_VS_MINOR_SHIFT      8
#define NVME_VS_COMPONENT_MASK   0xFFU

void nvme_init()
{
    struct pci_device *dev =
        pci_find_device(NVME_PCI_CLASS, NVME_PCI_SUBCLASS);

    if (dev == NULL || dev->prog_if != NVME_PCI_PROG_IF) {
        kprintf("NVME: Controller not found.\n");
        return;
    }

    kprintf("NVME: Controller found.\n");
    
    const uint32_t bar0 =
        pci_device_read(dev, PCI_REG4_OFFSET, 4);

    if (bar0 & PCI_BAR_IO_SPACE) {
        kprintf("NVME: Invalid BAR0 address space.\n");
        return;
    }

    if ((bar0 & PCI_BAR_TYPE_MASK) != PCI_BAR_TYPE_64BIT) {
        kprintf("NVME: Expected a 64-bit BAR.\n");
        return;
    }

    const uint32_t bar1 =
        pci_device_read(dev, PCI_REG5_OFFSET, 4);

    // Combine both BARs to obtain the physical register address.
    const uint64_t physical_address =
        ((uint64_t)bar1 << 32) |
        (bar0 & PCI_BAR_ADDRESS_MASK);
  
    kprintf("NVME: Physical address: 0x%x\n", physical_address);

    // Map the first page of the controller's register space.
    void *registers = vm_map_mmio(physical_address, PAGE_SIZE);

    if (registers == NULL) {
        kprintf("NVME: MMIO mapping failed.\n");
        return;
    }

    kprintf("NVME: MMIO mapping successful.\n");
    kprintf("NVME: Virtual address: 0x%x\n", (uint64_t)registers);

    // Read the controller version register.
    const uint32_t version =
        *(volatile uint32_t *)(
            (uint64_t)registers + NVME_REG_VS
        );

    const uint32_t major = version >> NVME_VS_MAJOR_SHIFT;
    const uint32_t minor =
        (version >> NVME_VS_MINOR_SHIFT) &
        NVME_VS_COMPONENT_MASK;
    
    const uint32_t tertiary =
        version & NVME_VS_COMPONENT_MASK;

    kprintf("NVME: Version %d.%d.%d\n",
        (int)major, (int)minor, (int)tertiary);
}