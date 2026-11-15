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
#include <stdbool.h>
#include <str.h>
#include <mem.h>
#include <dma.h>

#include "internal.h"

// Read a single logical block into a DMA buffer.
bool nvme_read_block(nvme_controller_t *ctrl,
                     uint64_t lba,
                     dma_buffer_t *buffer)
{
    if (!ctrl->namespace.valid || ctrl->io_queue.failed) {
        return false;
    }

    // Validate the logical block address.
    if (lba >= ctrl->namespace.block_count) {
        nvme_log("NVME: LBA out of range.\n");
        return false;
    }

    // Ensure the transfer fits within one DMA page.
    if (buffer == NULL ||
        buffer->virt_addr == NULL ||
        buffer->size < ctrl->namespace.block_size ||
        ctrl->namespace.block_size > PAGE_SIZE ||
        buffer->phys_addr % PAGE_SIZE != 0) {
        nvme_log("NVME: Invalid read buffer.\n");
        return false;
    }

    nvme_command_t command = {0};
    nvme_completion_t completion = {0};

    command.opcode = NVME_NVM_READ;
    command.namespace_id = ctrl->namespace.id;
    command.prp1 = buffer->phys_addr;

    // Encode the 64-bit starting logical block address.
    command.cdw10 = (uint32_t)lba;
    command.cdw11 = (uint32_t)(lba >> NVME_SLBA_HIGH_SHIFT);

    // NLB is zero-based. Zero represents one logical block.
    command.cdw12 = 0;

    return nvme_execute_command(ctrl,
                                &ctrl->io_queue,
                                &command,
                                &completion);
}

// Write a single logical block from a DMA buffer.
bool nvme_write_block(nvme_controller_t *ctrl,
                      uint64_t lba,
                      dma_buffer_t *buffer)
{
    if (!ctrl->namespace.valid || ctrl->io_queue.failed) {
        return false;
    }

    // Validate the logical block address.
    if (lba >= ctrl->namespace.block_count) {
        nvme_log("NVME: LBA out of range.\n");
        return false;
    }

    // Ensure the transfer fits within one DMA page.
    if (buffer == NULL ||
        buffer->virt_addr == NULL ||
        buffer->size < ctrl->namespace.block_size ||
        ctrl->namespace.block_size > PAGE_SIZE ||
        buffer->phys_addr % PAGE_SIZE != 0) {
        nvme_log("NVME: Invalid write buffer.\n");
        return false;
    }

    nvme_command_t command = {0};
    nvme_completion_t completion = {0};

    command.opcode = NVME_NVM_WRITE;
    command.namespace_id = ctrl->namespace.id;
    command.prp1 = buffer->phys_addr;

    // Encode the 64-bit starting logical block address.
    command.cdw10 = (uint32_t)lba;
    command.cdw11 = (uint32_t)(lba >> NVME_SLBA_HIGH_SHIFT);

    // NLB is zero-based. Zero represents one logical block.
    command.cdw12 = 0;

    return nvme_execute_command(ctrl,
                                &ctrl->io_queue,
                                &command,
                                &completion);
}

bool nvme_block_read(void *context,
                     uint64_t lba,
                     uint32_t count,
                     void *buffer)
{
    nvme_controller_t *ctrl = context;

    // Allocate a reusable DMA buffer for one logical block.
    dma_buffer_t dma = {0};

    if (!dma_alloc(ctrl->namespace.block_size, &dma)) {
        return false;
    }

    uint8_t *destination = buffer;

    // Read each requested logical block sequentially.
    for (uint32_t i = 0; i < count; i++) {

        if (!nvme_read_block(ctrl, lba + i, &dma)) {
            if (!ctrl->io_queue.failed) {
                dma_free(&dma);
            }

            return false;
        }

        // Copy the DMA data into the caller's buffer.
        memcpy(destination,
                dma.virt_addr,
                ctrl->namespace.block_size);

        destination += ctrl->namespace.block_size;
    }

    dma_free(&dma);

    return true;
}

bool nvme_block_write(void *context,
                      uint64_t lba,
                      uint32_t count,
                      const void *buffer)
{
    nvme_controller_t *ctrl = context;

    // Allocate a reusable DMA buffer for one logical block.
    dma_buffer_t dma = {0};

    if (!dma_alloc(ctrl->namespace.block_size, &dma)) {
        return false;
    }

    const uint8_t *source = buffer;

    // Write each requested logical block sequentially.
    for (uint32_t i = 0; i < count; i++) {

        // Copy the caller's data into the DMA buffer.
        memcpy(dma.virt_addr,
               source,
               ctrl->namespace.block_size);

        if (!nvme_write_block(ctrl, lba + i, &dma)) {
            if (!ctrl->io_queue.failed) {
                dma_free(&dma);
            }

            return false;
        }

        source += ctrl->namespace.block_size;
    }

    dma_free(&dma);

    return true;
}