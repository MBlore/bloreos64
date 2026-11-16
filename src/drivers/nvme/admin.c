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

    nvme_log("NVME: I/O submission queues: %d\n",
        (int)submission_queues);

    nvme_log("NVME: I/O completion queues: %d\n",
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
        nvme_log("NVME: Identify buffer allocation failed.\n");
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
        nvme_log("NVME: Identify buffer retained for safety.\n");
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

        nvme_log("NVME: Controller identification successful.\n");
        nvme_log("NVME: Model: %s\n", model);
        nvme_log("NVME: Serial: %s\n", serial);
        nvme_log("NVME: Firmware: %s\n", firmware);
        nvme_log("NVME: Number of namespaces: %lu\n",
            (uint64_t)namespaces);
    }

    dma_free(&buffer);

    return success;
}

// Discover the active namespaces attached to the controller.
bool nvme_identify_active_namespaces(nvme_controller_t *ctrl)
{
    dma_buffer_t buffer = {0};

    ctrl->namespace_count = 0;

    if (ctrl->admin_queue.failed) {
        return false;
    }

    if (!dma_alloc(NVME_IDENTIFY_DATA_SIZE, &buffer)) {
        nvme_log("NVME: Namespace list allocation failed.\n");
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
        nvme_log("NVME: Identify buffer retained for safety.\n");
        return false;
    }

    if (success) {
        const uint32_t *namespaces = buffer.virt_addr;

        const size_t max_entries =
            NVME_IDENTIFY_DATA_SIZE / sizeof(uint32_t);

        for (size_t i = 0; i < max_entries; i++) {
            if (namespaces[i] == 0) {
                break;
            }

            if (ctrl->namespace_count >= NVME_MAX_NAMESPACES) {
                kprintf("NVME: Namespace limit reached.\n");
                break;
            }

            // Persist the discovered namespace identifier.
            ctrl->namespace_ids[ctrl->namespace_count++] = namespaces[i];

            nvme_log("NVME: Active namespace ID: %lu\n",
                (uint64_t)namespaces[i]);
        }

        kprintf("NVME: Active namespaces: %lu\n", (uint64_t)ctrl->namespace_count);
    }

    dma_free(&buffer);

    return success;
}

// Retrieve identification data for a specific namespace.
bool nvme_identify_namespace(nvme_controller_t *ctrl,
                             uint32_t namespace_id,
                            nvme_namespace_t *ns)
{
    dma_buffer_t buffer = {0};
    ns->valid = false;

    if (ctrl->admin_queue.failed) {
        return false;
    }

    if (!dma_alloc(NVME_IDENTIFY_DATA_SIZE, &buffer)) {
        nvme_log("NVME: Identify buffer allocation failed.\n");
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
        nvme_log("NVME: Identify buffer retained for safety.\n");
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

        nvme_log("NVME: Namespace identification successful.\n");
        nvme_log("NVME: Namespace ID: %lu\n", (uint64_t)namespace_id);
        nvme_log("NVME: Namespace size: %lu blocks\n", size);
        nvme_log("NVME: Namespace capacity: %lu blocks\n", capacity);
        nvme_log("NVME: Namespace utilisation: %lu blocks\n", utilisation);

        // Determine the currently active logical block format.
        const uint8_t format =
            data[NVME_IDENTIFY_FLBAS_OFFSET] &
            NVME_IDENTIFY_FLBAS_MASK;

        // NLBAF is a zero-based format count.
        if (format > data[NVME_IDENTIFY_NLBAF_OFFSET]) {
            nvme_log("NVME: Invalid LBA format.\n");
            dma_free(&buffer);
            return false;
        }

        // Locate the selected LBA format descriptor.
        const size_t format_offset =
            NVME_IDENTIFY_LBAF_OFFSET +
            (format * NVME_IDENTIFY_LBAF_SIZE);

        // Retrieve the metadata size for the active LBA format.
        const uint16_t metadata_size =
            *(const uint16_t *)(data + format_offset +
                                NVME_IDENTIFY_MS_OFFSET);

        // Retrieve the active protection information type.
        const uint8_t protection_type =
            data[NVME_IDENTIFY_DPS_OFFSET] &
            NVME_IDENTIFY_DPS_PI_MASK;

        if (metadata_size != NVME_METADATA_NONE ||
            protection_type != NVME_PROTECTION_NONE) {
            nvme_log("NVME: Unsupported namespace metadata format.\n");
            dma_free(&buffer);
            return false;
        }

        nvme_log("NVME: Metadata size: %d bytes\n",
            (int)metadata_size);

        nvme_log("NVME: Protection type: %d\n",
            (int)protection_type);

        // Read the logical block size exponent.
        const uint8_t lbads =
            data[format_offset + NVME_IDENTIFY_LBADS_OFFSET];

        if (lbads < NVME_IDENTIFY_LBADS_MIN || lbads >= 32) {
            nvme_log("NVME: Unsupported logical block size.\n");
            dma_free(&buffer);
            return false;
        }

        const uint32_t block_size = 1U << lbads;

        nvme_log("NVME: Active LBA format: %d\n", (int)format);
        nvme_log("NVME: Logical block size: %lu bytes\n",
            (uint64_t)block_size);

        // Calculate the total addressable storage in bytes.
        if (size > UINT64_MAX / block_size) {
            nvme_log("NVME: Namespace size overflow.\n");
            dma_free(&buffer);
            return false;
        }

        const uint64_t size_bytes = size * block_size;

        nvme_log("NVME: Disk size: %lu bytes\n", size_bytes);

        // Persist the namespace geometry for future I/O operations.
        ns->id = namespace_id;
        ns->block_size = block_size;
        ns->block_count = size;
        ns->capacity_bytes = size_bytes;
        ns->valid = true;
    }

    dma_free(&buffer);

    return success;
}

// Negotiate the number of I/O queues with the controller.
bool nvme_set_number_of_queues(nvme_controller_t *ctrl)
{
    nvme_command_t command = {0};
    nvme_completion_t completion = {0};

    const uint32_t requested =
        NVME_IO_QUEUE_PAIR_COUNT - 1;

    command.opcode = NVME_ADMIN_SET_FEATURES;
    command.cdw10 = NVME_FEATURE_NUM_QUEUES;

    // Both requested queue counts are zero-based.
    command.cdw11 =
        (requested << NVME_FEATURE_NCQ_SHIFT) |
        requested;

    if (!nvme_execute_command(ctrl,
                              &ctrl->admin_queue,
                              &command,
                              &completion)) {
        return false;
    }

    // Decode the queue counts allocated by the controller.
    const uint32_t submission_queues =
        (completion.result & NVME_FEATURE_NSQ_MASK) + 1;

    const uint32_t completion_queues =
        (completion.result >> NVME_FEATURE_NCQ_SHIFT) + 1;

    if (submission_queues < NVME_IO_QUEUE_PAIR_COUNT ||
        completion_queues < NVME_IO_QUEUE_PAIR_COUNT) {
        nvme_log("NVME: Insufficient I/O queues.\n");
        return false;
    }

    nvme_log("NVME: Allocated I/O submission queues: %d\n",
        (int)submission_queues);

    nvme_log("NVME: Allocated I/O completion queues: %d\n",
        (int)completion_queues);

    return true;
}

// Register an I/O completion queue with the controller.
bool nvme_create_io_completion_queue(nvme_controller_t *ctrl)
{
    nvme_command_t command = {0};
    nvme_completion_t completion = {0};

    nvme_queue_t *queue = &ctrl->io_queue;

    const uint32_t queue_size = queue->entry_count - 1;

    command.opcode = NVME_ADMIN_CREATE_IO_CQ;
    command.prp1 = queue->completion.phys_addr;

    // Encode the queue size and identifier.
    command.cdw10 =
        (queue_size << NVME_QUEUE_SIZE_SHIFT) |
        queue->queue_id;

    // Use physically contiguous memory without interrupts.
    command.cdw11 = NVME_QUEUE_PHYS_CONTIGUOUS;

    if (!nvme_execute_command(ctrl,
                              &ctrl->admin_queue,
                              &command,
                              &completion)) {
        return false;
    }

    nvme_log("NVME: I/O completion queue created.\n");

    return true;
}

// Register an I/O submission queue with the controller.
bool nvme_create_io_submission_queue(nvme_controller_t *ctrl)
{
    nvme_command_t command = {0};
    nvme_completion_t completion = {0};

    nvme_queue_t *queue = &ctrl->io_queue;

    const uint32_t queue_size = queue->entry_count - 1;

    command.opcode = NVME_ADMIN_CREATE_IO_SQ;
    command.prp1 = queue->submission.phys_addr;

    // Encode the queue size and identifier.
    command.cdw10 =
        (queue_size << NVME_QUEUE_SIZE_SHIFT) |
        queue->queue_id;

    // Associate the submission queue with its completion queue.
    command.cdw11 =
        ((uint32_t)queue->queue_id << NVME_QUEUE_CQID_SHIFT) |
        NVME_QUEUE_PHYS_CONTIGUOUS;

    if (!nvme_execute_command(ctrl,
                              &ctrl->admin_queue,
                              &command,
                              &completion)) {
        return false;
    }

    nvme_log("NVME: I/O submission queue created.\n");

    return true;
}