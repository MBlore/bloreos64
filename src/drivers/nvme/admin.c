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

#include "internal.h"

// Retrieve the currently allocated number of I/O queues.
bool nvme_get_number_of_queues(nvme_controller_t *ctrl)
{
    nvme_command_t command = {0};
    nvme_completion_t completion = {0};

    command.opcode = NVME_ADMIN_GET_FEATURES;
    command.cdw10 = NVME_FEATURE_NUM_QUEUES;

    if (!nvme_execute_command(ctrl,
                              &ctrl->admin_queue,
                              &command,
                              &completion)) {
        return false;
    }

    // The returned queue counts are zero-based.
    const uint32_t submission_queues =
        (completion.result & NVME_FEATURE_NSQ_MASK) + 1;

    const uint32_t completion_queues =
        (completion.result >> NVME_FEATURE_NCQ_SHIFT) + 1;

    kprintf("NVME: I/O submission queues: %d\n",
        (int)submission_queues);

    kprintf("NVME: I/O completion queues: %d\n",
        (int)completion_queues);

    return true;
}

// Execute an Identify command using a DMA data buffer.
static bool nvme_identify(nvme_controller_t *ctrl,
                          uint32_t namespace_id,
                          uint8_t cns,
                          dma_buffer_t *buffer)
{
    nvme_command_t command = {0};
    nvme_completion_t completion = {0};

    command.opcode = NVME_ADMIN_IDENTIFY;
    command.namespace_id = namespace_id;
    command.prp1 = buffer->phys_addr;
    command.cdw10 = cns;

    return nvme_execute_command(ctrl,
                                &ctrl->admin_queue,
                                &command,
                                &completion);
}

// Extract a space-padded NVMe string.
static void nvme_read_string(char *dest,
                             const uint8_t *src,
                             size_t length)
{
    for (size_t i = 0; i < length; i++) {
        dest[i] = (char)src[i];
    }

    // Remove trailing padding spaces.
    while (length > 0 && dest[length - 1] == ' ') {
        length--;
    }

    dest[length] = '\0';
}

// Retrieve identification data from the NVMe controller.
bool nvme_identify_controller(nvme_controller_t *ctrl)
{
    dma_buffer_t buffer = {0};

    if (ctrl->admin_queue.failed) {
        return false;
    }

    if (!dma_alloc(NVME_IDENTIFY_DATA_SIZE, &buffer)) {
        kprintf("NVME: Identify buffer allocation failed.\n");
        return false;
    }

    const bool success = nvme_identify(
        ctrl,
        0,
        NVME_IDENTIFY_CNS_CONTROLLER,
        &buffer
    );

    if (ctrl->admin_queue.failed) {
        // The controller may still be accessing the DMA buffer.
        // Do not release this memory until the controller is reset.
        kprintf("NVME: Identify buffer retained for safety.\n");
        return false;
    }

    if (success) {
        const uint8_t *data = buffer.virt_addr;

        char serial[NVME_IDENTIFY_SN_LENGTH + 1];
        char model[NVME_IDENTIFY_MN_LENGTH + 1];
        char firmware[NVME_IDENTIFY_FR_LENGTH + 1];

        nvme_read_string(serial,
            data + NVME_IDENTIFY_SN_OFFSET,
            NVME_IDENTIFY_SN_LENGTH);

        nvme_read_string(model,
            data + NVME_IDENTIFY_MN_OFFSET,
            NVME_IDENTIFY_MN_LENGTH);

        nvme_read_string(firmware,
            data + NVME_IDENTIFY_FR_OFFSET,
            NVME_IDENTIFY_FR_LENGTH);

        const uint32_t namespaces =
            *(const uint32_t *)(data + NVME_IDENTIFY_NN_OFFSET);

        kprintf("NVME: Controller identification successful.\n");
        kprintf("NVME: Model: %s\n", model);
        kprintf("NVME: Serial: %s\n", serial);
        kprintf("NVME: Firmware: %s\n", firmware);
        kprintf("NVME: Number of namespaces: %lu\n",
            (uint64_t)namespaces);
    }

    dma_free(&buffer);

    return success;
}

// Discover the active namespaces attached to the controller.
bool nvme_identify_active_namespaces(nvme_controller_t *ctrl)
{
    dma_buffer_t buffer = {0};

    if (ctrl->admin_queue.failed) {
        return false;
    }

    if (!dma_alloc(NVME_IDENTIFY_DATA_SIZE, &buffer)) {
        kprintf("NVME: Namespace list allocation failed.\n");
        return false;
    }

    const bool success = nvme_identify(
        ctrl,
        0,
        NVME_IDENTIFY_CNS_ACTIVE_LIST,
        &buffer
    );

    if (ctrl->admin_queue.failed) {
        // The controller may still be accessing this buffer.
        kprintf("NVME: Identify buffer retained for safety.\n");
        return false;
    }

    if (success) {
        const uint32_t *namespaces = buffer.virt_addr;

        const size_t max_entries =
            NVME_IDENTIFY_DATA_SIZE / sizeof(uint32_t);

        size_t count = 0;

        for (size_t i = 0; i < max_entries; i++) {
            if (namespaces[i] == 0) {
                break;
            }

            kprintf("NVME: Active namespace ID: %lu\n",
                (uint64_t)namespaces[i]);

            count++;
        }

        kprintf("NVME: Active namespaces: %lu\n",
            (uint64_t)count);
    }

    dma_free(&buffer);

    return success;
}

// Retrieve identification data for a specific namespace.
bool nvme_identify_namespace(nvme_controller_t *ctrl,
                             uint32_t namespace_id)
{
    dma_buffer_t buffer = {0};

    if (ctrl->admin_queue.failed) {
        return false;
    }

    if (!dma_alloc(NVME_IDENTIFY_DATA_SIZE, &buffer)) {
        kprintf("NVME: Identify buffer allocation failed.\n");
        return false;
    }

    const bool success = nvme_identify(
        ctrl,
        namespace_id,
        NVME_IDENTIFY_CNS_NAMESPACE,
        &buffer
    );

    if (ctrl->admin_queue.failed) {
        // The controller may still be accessing this buffer.
        kprintf("NVME: Identify buffer retained for safety.\n");
        return false;
    }

    if (success) {
        const uint8_t *data = buffer.virt_addr;

        const uint64_t size =
            *(const uint64_t *)(data + NVME_IDENTIFY_NSZE_OFFSET);

        const uint64_t capacity =
            *(const uint64_t *)(data + NVME_IDENTIFY_NCAP_OFFSET);

        const uint64_t utilisation =
            *(const uint64_t *)(data + NVME_IDENTIFY_NUSE_OFFSET);

        kprintf("NVME: Namespace identification successful.\n");
        kprintf("NVME: Namespace ID: %lu\n", (uint64_t)namespace_id);
        kprintf("NVME: Namespace size: %lu blocks\n", size);
        kprintf("NVME: Namespace capacity: %lu blocks\n", capacity);
        kprintf("NVME: Namespace utilisation: %lu blocks\n", utilisation);

        // Determine the currently active logical block format.
        const uint8_t format =
            data[NVME_IDENTIFY_FLBAS_OFFSET] &
            NVME_IDENTIFY_FLBAS_MASK;

        // NLBAF is a zero-based format count.
        if (format > data[NVME_IDENTIFY_NLBAF_OFFSET]) {
            kprintf("NVME: Invalid LBA format.\n");
            dma_free(&buffer);
            return false;
        }

        // Locate the selected LBA format descriptor.
        const size_t format_offset =
            NVME_IDENTIFY_LBAF_OFFSET +
            (format * NVME_IDENTIFY_LBAF_SIZE);

        // Read the logical block size exponent.
        const uint8_t lbads =
            data[format_offset + NVME_IDENTIFY_LBADS_OFFSET];

        if (lbads < NVME_IDENTIFY_LBADS_MIN || lbads >= 32) {
            kprintf("NVME: Unsupported logical block size.\n");
            dma_free(&buffer);
            return false;
        }

        const uint32_t block_size = 1U << lbads;

        kprintf("NVME: Active LBA format: %d\n", (int)format);
        kprintf("NVME: Logical block size: %lu bytes\n",
            (uint64_t)block_size);

        // Calculate the total addressable storage in bytes.
        if (size > UINT64_MAX / block_size) {
            kprintf("NVME: Namespace size overflow.\n");
            dma_free(&buffer);
            return false;
        }

        const uint64_t size_bytes = size * block_size;

        kprintf("NVME: Disk size: %lu bytes\n", size_bytes);
    }

    dma_free(&buffer);

    return success;
}