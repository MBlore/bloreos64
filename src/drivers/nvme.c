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

void nvme_init()
{
    struct pci_device* dev = pci_find_device(0x01, 0x08);
    if (dev == NULL) {
        kprintf("NVME: Controller not found.\n");
        return;
    }

    kprintf("NVME Header Type: 0x%X\n", dev->header_type);

    // Get the command status.
    // Set the command status.
    // Read BAR addresses.
    // Restore command status.

    //kprintf("BAR0: 0x%X\n", dev->bar0_address);
    //kprintf("BAR1: 0x%X\n", dev->bar1_address);
    
    kprintf("NVME: Initialized.\n");
}