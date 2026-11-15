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
#include <hpet.h>
#include <dma.h>

#include "internal.h"

static nvme_controller_t controller;

void nvme_init()
{
    struct pci_device *dev =
        pci_find_device(NVME_PCI_CLASS, NVME_PCI_SUBCLASS);

    if (dev == NULL || dev->prog_if != NVME_PCI_PROG_IF) {
        kprintf("NVME: Controller not found.\n");
        return;
    }

    kprintf("NVME: Controller found.\n");

    // Enable MMIO access and device-initiated DMA.
    if (!nvme_enable_pci(dev)) {
        return;
    }

    // Map the bar registers.    
    void *registers = nvme_map_registers(dev);
    if (registers == NULL) {
        return;
    }

    // Read the controller version register.
    nvme_read_version(registers);

    controller.registers = registers;

    if (!nvme_read_capabilities(&controller)) {
        return;
    }

    if (!nvme_reset_controller(&controller)) {
        return;
    }

    if (controller.max_queue_entries < NVME_ADMIN_QUEUE_ENTRIES) {
        kprintf("NVME: Insufficient queue capacity.\n");
        return;
    }

    if (!nvme_allocate_queue(&controller.admin_queue,
                            NVME_ADMIN_QUEUE_ID,
                            NVME_ADMIN_QUEUE_ENTRIES)) {
        return;
    }

    kprintf("NVME: Administration queues allocated.\n");

    kprintf("NVME: SQ physical address: 0x%x\n",
        controller.admin_queue.submission.phys_addr);

    kprintf("NVME: CQ physical address: 0x%x\n",
        controller.admin_queue.completion.phys_addr);

    if (!nvme_configure_admin_queues(&controller)) {
        dma_free(&controller.admin_queue.submission);
        dma_free(&controller.admin_queue.completion);
        return;
    }

    if (!nvme_enable_controller(&controller)) {
        return;
    }

    nvme_mask_interrupts(&controller);

    kprintf("NVME: Executing first administration command...\n");

    if (!nvme_get_number_of_queues(&controller)) {
        return;
    }

    kprintf("NVME: Administration command successful.\n");
}