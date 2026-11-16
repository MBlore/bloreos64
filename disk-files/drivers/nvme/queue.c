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
#include <dma.h>
#include <hpet.h>

#include "internal.h"

// Allocate and initialise an NVMe queue pair.
bool nvme_allocate_queue(nvme_queue_t *queue,
                                uint16_t queue_id,
                                uint16_t entry_count)
{
    // Allocate the submission queue.
    if (!dma_alloc(entry_count * sizeof(nvme_command_t),
                   &queue->submission)) {
        nvme_log("NVME: Submission queue allocation failed.\n");
        return false;
    }

    // Allocate the completion queue.
    if (!dma_alloc(entry_count * sizeof(nvme_completion_t),
                   &queue->completion)) {
        nvme_log("NVME: Completion queue allocation failed.\n");

        dma_free(&queue->submission);
        return false;
    }

    // Initialise the queue management state.
    queue->queue_id = queue_id;
    queue->entry_count = entry_count;
    queue->sq_tail = 0;
    queue->cq_head = 0;
    queue->cq_phase = 1;
    queue->next_command_id = 0;
    queue->failed = false;

    return true;
}

// Notify the controller of a queue pointer update.
static void nvme_ring_doorbell(nvme_controller_t *ctrl,
                               uint16_t queue_id,
                               uint32_t doorbell_type,
                               uint16_t value)
{
    const uint64_t offset =
        NVME_REG_DOORBELL_BASE +
        ((queue_id * NVME_DOORBELLS_PER_QUEUE +
          doorbell_type) * ctrl->doorbell_stride);

    // Doorbell registers require a 32-bit MMIO write.
    *(volatile uint32_t *)(
        (uint64_t)ctrl->registers + offset
    ) = value;
}

// Place a command into the submission queue.
static void nvme_submit_command(nvme_controller_t *ctrl,
                                 nvme_queue_t *queue,
                                 const nvme_command_t *command)
{
    nvme_command_t *entries =
        (nvme_command_t *)queue->submission.virt_addr;

    // Copy the command into the next available entry.
    entries[queue->sq_tail] = *command;

    // Advance the tail, wrapping around if necessary.
    queue->sq_tail =
        (queue->sq_tail + 1) % queue->entry_count;

    // Ensure the command is visible before notifying the controller.
    __asm__ volatile("sfence" ::: "memory");

    // Notify the controller of the new submission queue tail.
    nvme_ring_doorbell(ctrl,
                       queue->queue_id,
                       NVME_DOORBELL_SQ_TAIL,
                       queue->sq_tail);
}

// Wait for the controller to produce a completion entry.
static bool nvme_wait_completion(nvme_queue_t *queue,
                                 nvme_completion_t *completion)
{
    volatile nvme_completion_t *entries =
        (volatile nvme_completion_t *)queue->completion.virt_addr;

    const uint64_t start = hpet_get_uptime_ms();

    while (true) {
        const uint16_t status =
            entries[queue->cq_head].status;

        // Check whether the controller has produced a new entry.
        if ((status & NVME_COMPLETION_PHASE_MASK) ==
            queue->cq_phase) {

            // Order subsequent reads after observing the phase bit.
            __asm__ volatile("lfence" ::: "memory");

            *completion = entries[queue->cq_head];

            return true;
        }

        if (hpet_get_uptime_ms() - start >=
            NVME_COMMAND_TIMEOUT_MS) {
            nvme_log("NVME: Command completion timed out.\n");
            return false;
        }

        __asm__ volatile("pause");
    }
}

// Acknowledge a processed completion queue entry.
static void nvme_acknowledge_completion(
    nvme_controller_t *ctrl,
    nvme_queue_t *queue)
{
    // Advance the completion queue head.
    queue->cq_head++;

    // Wrap around and toggle the expected phase bit.
    if (queue->cq_head == queue->entry_count) {
        queue->cq_head = 0;
        queue->cq_phase ^= NVME_COMPLETION_PHASE_MASK;
    }

    // Ensure completion reads finish before releasing the entry.
    __asm__ volatile("mfence" ::: "memory");

    // Notify the controller of the new completion queue head.
    nvme_ring_doorbell(ctrl,
                       queue->queue_id,
                       NVME_DOORBELL_CQ_HEAD,
                       queue->cq_head);
}

// Execute a command synchronously and validate its completion.
bool nvme_execute_command(
    nvme_controller_t *ctrl,
    nvme_queue_t *queue,
    nvme_command_t *command,
    nvme_completion_t *completion)
{
    // Do not reuse a queue that has encountered a fatal error.
    if (queue->failed) {
        return false;
    }

    // Assign a command identifier.
    command->command_id = queue->next_command_id++;

    nvme_submit_command(ctrl, queue, command);

    // Wait for the controller to complete the command.
    if (!nvme_wait_completion(queue, completion)) {
        queue->failed = true;
        return false;
    }

    nvme_acknowledge_completion(ctrl, queue);

    // Verify that the completion belongs to our command.
    if (completion->command_id != command->command_id ||
        completion->sq_id != queue->queue_id) {
        nvme_log("NVME: Unexpected command completion.\n");
        queue->failed = true;
        return false;
    }

    // Check whether the controller completed the command successfully.
    if (completion->status & NVME_COMPLETION_STATUS_MASK) {
        nvme_log("NVME: Command failed. Status: 0x%x\n",
            (uint64_t)completion->status);
        return false;
    }

    return true;
}