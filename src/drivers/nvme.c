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

#define NVME_PCI_CLASS     0x01
#define NVME_PCI_SUBCLASS  0x08
#define NVME_PCI_PROG_IF   0x02

#define PCI_BAR_IO_SPACE        (1U << 0)
#define PCI_BAR_TYPE_MASK       (3U << 1)
#define PCI_BAR_TYPE_64BIT      (2U << 1)
#define PCI_BAR_ADDRESS_MASK    0xFFFFFFF0U

#define NVME_REG_CAP 0x00
#define NVME_REG_VS  0x08
#define NVME_REG_CC   0x14
#define NVME_REG_CSTS 0x1C
#define NVME_REG_AQA  0x24
#define NVME_REG_ASQ  0x28
#define NVME_REG_ACQ  0x30
#define NVME_REG_INTMS 0x0C
#define NVME_INTERRUPT_MASK_ALL 0xFFFFFFFFU
#define NVME_REG_DOORBELL_BASE 0x1000
#define NVME_MMIO_PAGE_COUNT  2

#define NVME_DOORBELLS_PER_QUEUE 2
#define NVME_DOORBELL_SQ_TAIL   0
#define NVME_DOORBELL_CQ_HEAD   1

#define NVME_AQA_ACQS_SHIFT     16
#define NVME_VS_MAJOR_SHIFT     16
#define NVME_VS_MINOR_SHIFT      8
#define NVME_VS_COMPONENT_MASK   0xFFU

#define NVME_CAP_MQES_MASK          0xFFFFULL
#define NVME_CAP_TO_SHIFT           24
#define NVME_CAP_TO_MASK            0xFFULL
#define NVME_CAP_TO_UNIT_MS         500
#define NVME_CAP_DSTRD_SHIFT        32
#define NVME_CAP_DSTRD_MASK         0xFULL
#define NVME_DOORBELL_BASE_SIZE     4
#define NVME_CAP_MPSMIN_SHIFT       48
#define NVME_CAP_MPSMAX_SHIFT       52
#define NVME_CAP_MPS_MASK           0xFULL
#define NVME_MPS_BASE_SHIFT         12
#define NVME_CAP_CSS_NVM            (1ULL << 37)

// Controller config and status flags.
#define NVME_CC_EN    (1U << 0)
#define NVME_CSTS_RDY (1U << 0)
#define NVME_CSTS_CFS (1U << 1)

#define NVME_CC_CSS_SHIFT     4
#define NVME_CC_MPS_SHIFT     7
#define NVME_CC_AMS_SHIFT     11
#define NVME_CC_IOSQES_SHIFT  16
#define NVME_CC_IOCQES_SHIFT  20

#define NVME_CC_CSS_NVM         0
#define NVME_CC_MPS_4K          0
#define NVME_CC_AMS_ROUND_ROBIN 0

#define NVME_IOSQES_64_BYTES 6
#define NVME_IOCQES_16_BYTES 4

#define NVME_COMPLETION_PHASE_MASK (1U << 0)
#define NVME_COMMAND_TIMEOUT_MS    5000
#define NVME_COMPLETION_STATUS_MASK 0x0FFEU

#define NVME_ADMIN_GET_FEATURES  0x0A
#define NVME_FEATURE_NUM_QUEUES  0x07
#define NVME_FEATURE_NSQ_MASK    0xFFFFU
#define NVME_FEATURE_NCQ_SHIFT   16

// Represents a single 64-byte NVMe submission queue entry.
typedef struct
{
    uint8_t opcode;
    uint8_t flags;
    uint16_t command_id;

    uint32_t namespace_id;
    uint32_t cdw2;
    uint32_t cdw3;

    uint64_t metadata_ptr;
    uint64_t prp1;
    uint64_t prp2;

    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_command_t;

#define NVME_COMMAND_SIZE 64

_Static_assert(
    sizeof(nvme_command_t) == NVME_COMMAND_SIZE,
    "Invalid NVMe command structure size"
);

// Represents a single 16-byte NVMe completion queue entry.
typedef struct
{
    uint32_t result;
    uint32_t cdw1;

    uint16_t sq_head;
    uint16_t sq_id;

    uint16_t command_id;
    uint16_t status;
} nvme_completion_t;

#define NVME_COMPLETION_SIZE 16

_Static_assert(
    sizeof(nvme_completion_t) == NVME_COMPLETION_SIZE,
    "Invalid NVMe completion structure size"
);

// Represents an NVMe submission and completion queue pair.
typedef struct
{
    dma_buffer_t submission;
    dma_buffer_t completion;

    uint16_t queue_id;
    uint16_t entry_count;

    uint16_t sq_tail;
    uint16_t cq_head;

    uint8_t cq_phase;

    uint16_t next_command_id;
    bool failed;
} nvme_queue_t;

#define NVME_ADMIN_QUEUE_ENTRIES 64
#define NVME_ADMIN_QUEUE_ID 0

// Represents an NVMe controller and its runtime state.
typedef struct
{
    void *registers;

    uint32_t max_queue_entries;
    uint32_t timeout_ms;
    uint32_t doorbell_stride;

    nvme_queue_t admin_queue;
} nvme_controller_t;

static nvme_controller_t controller;

// Wait for the controller to enter the requested ready state.
static bool nvme_wait_ready(void *registers,
                            bool ready,
                            uint32_t timeout_ms)
{
    const uint64_t start = hpet_get_uptime_ms();

    while (true) {
        const uint32_t status =
            *(volatile uint32_t *)(
                (uint64_t)registers + NVME_REG_CSTS
            );

        const bool current_ready =
            (status & NVME_CSTS_RDY) != 0;

        if (current_ready == ready) {
            return true;
        }

        if (hpet_get_uptime_ms() - start >= timeout_ms) {
            return false;
        }

        __asm__ volatile("pause");
    }
}

// Enable the PCI permissions required by the NVMe controller.
static bool nvme_enable_pci(struct pci_device *dev)
{
    uint16_t pci_command =
        pci_device_read(dev, PCI_REG1_OFFSET, 2);

    const uint16_t required_flags =
        PCI_COMMAND_MEMORY_SPACE |
        PCI_COMMAND_BUS_MASTER;

    pci_command |= required_flags;

    pci_device_write(dev, PCI_REG1_OFFSET, 2, pci_command);

    // Verify that the device accepted the configuration.
    pci_command = pci_device_read(dev, PCI_REG1_OFFSET, 2);

    if ((pci_command & required_flags) != required_flags) {
        kprintf("NVME: Failed to enable PCI command flags.\n");
        return false;
    }

    kprintf("NVME: PCI memory space enabled.\n");
    kprintf("NVME: PCI bus mastering enabled.\n");

    return true;
}

// Map the NVMe controller's physical register space.
static void *nvme_map_registers(struct pci_device *dev)
{
    const uint32_t bar0 =
        pci_device_read(dev, PCI_REG4_OFFSET, 4);

    if (bar0 & PCI_BAR_IO_SPACE) {
        kprintf("NVME: Invalid BAR0 address space.\n");
        return NULL;
    }

    if ((bar0 & PCI_BAR_TYPE_MASK) != PCI_BAR_TYPE_64BIT) {
        kprintf("NVME: Expected a 64-bit BAR.\n");
        return NULL;
    }

    const uint32_t bar1 =
        pci_device_read(dev, PCI_REG5_OFFSET, 4);

    // Combine both BARs to obtain the physical register address.
    const uint64_t physical_address =
        ((uint64_t)bar1 << 32) |
        (bar0 & PCI_BAR_ADDRESS_MASK);

    kprintf("NVME: Physical address: 0x%x\n",
        physical_address);

    // Map the controller registers and doorbell registers.
    void *registers = vm_map_mmio(
        physical_address,
        PAGE_SIZE * NVME_MMIO_PAGE_COUNT
    );

    if (registers == NULL) {
        kprintf("NVME: MMIO mapping failed.\n");
        return NULL;
    }

    kprintf("NVME: MMIO mapping successful.\n");
    kprintf("NVME: Virtual address: 0x%x\n",
        (uint64_t)registers);

    return registers;
}

// Read and report the NVMe controller specification version.
static void nvme_read_version(void *registers)
{
    const uint32_t version =
        *(volatile uint32_t *)(
            (uint64_t)registers + NVME_REG_VS
        );

    const uint32_t major =
        version >> NVME_VS_MAJOR_SHIFT;

    const uint32_t minor =
        (version >> NVME_VS_MINOR_SHIFT) &
        NVME_VS_COMPONENT_MASK;

    const uint32_t tertiary =
        version & NVME_VS_COMPONENT_MASK;

    kprintf("NVME: Version %d.%d.%d\n",
        (int)major, (int)minor, (int)tertiary);
}

// Read and validate the controller's hardware capabilities.
static bool nvme_read_capabilities(nvme_controller_t *ctrl)
{
    const uint64_t capabilities =
        *(volatile uint64_t *)(
            (uint64_t)ctrl->registers + NVME_REG_CAP
        );

    kprintf("NVME: Capabilities: 0x%x\n", capabilities);

    // MQES is a zero-based value.
    ctrl->max_queue_entries =
        (capabilities & NVME_CAP_MQES_MASK) + 1;

    kprintf("NVME: Maximum queue entries: %d\n",
        (int)ctrl->max_queue_entries);

    // Controller readiness timeout in milliseconds.
    ctrl->timeout_ms =
        ((capabilities >> NVME_CAP_TO_SHIFT) &
        NVME_CAP_TO_MASK) * NVME_CAP_TO_UNIT_MS;

    kprintf("NVME: Ready timeout: %d ms\n",
        (int)ctrl->timeout_ms);

    // Calculate the spacing between doorbell registers.
    const uint32_t dstrd =
        (capabilities >> NVME_CAP_DSTRD_SHIFT) &
        NVME_CAP_DSTRD_MASK;

    ctrl->doorbell_stride =
        NVME_DOORBELL_BASE_SIZE << dstrd;

    kprintf("NVME: Doorbell stride: %d bytes\n",
        (int)ctrl->doorbell_stride);

    // Decode the supported memory page size range.
    const uint32_t mpsmin =
        (capabilities >> NVME_CAP_MPSMIN_SHIFT) &
        NVME_CAP_MPS_MASK;

    const uint32_t mpsmax =
        (capabilities >> NVME_CAP_MPSMAX_SHIFT) &
        NVME_CAP_MPS_MASK;

    const uint32_t min_page_size =
        1U << (NVME_MPS_BASE_SHIFT + mpsmin);

    const uint32_t max_page_size =
        1U << (NVME_MPS_BASE_SHIFT + mpsmax);

    kprintf("NVME: Minimum page size: %d bytes\n",
        (int)min_page_size);

    kprintf("NVME: Maximum page size: %d bytes\n",
        (int)max_page_size);

    // Validate the kernel's memory page size.
    if (PAGE_SIZE < min_page_size ||
        PAGE_SIZE > max_page_size) {
        kprintf("NVME: Unsupported memory page size.\n");
        return false;
    }

    // Validate support for the NVM Command Set.
    if (!(capabilities & NVME_CAP_CSS_NVM)) {
        kprintf("NVME: NVM Command Set not supported.\n");
        return false;
    }

    return true;
}

// Disable the controller and wait for it to enter a reset state.
static bool nvme_reset_controller(nvme_controller_t *ctrl)
{
    const uint32_t cc =
        *(volatile uint32_t *)(
            (uint64_t)ctrl->registers + NVME_REG_CC
        );

    const uint32_t csts =
        *(volatile uint32_t *)(
            (uint64_t)ctrl->registers + NVME_REG_CSTS
        );

    kprintf("NVME: Controller configuration: 0x%x\n",
        (uint64_t)cc);

    kprintf("NVME: Controller status: 0x%x\n",
        (uint64_t)csts);

    kprintf("NVME: Controller enabled: %s\n",
        (cc & NVME_CC_EN) ? "Yes" : "No");

    kprintf("NVME: Controller ready: %s\n",
        (csts & NVME_CSTS_RDY) ? "Yes" : "No");

    kprintf("NVME: Controller fatal error: %s\n",
        (csts & NVME_CSTS_CFS) ? "Yes" : "No");

    // Disable the controller.
    *(volatile uint32_t *)(
        (uint64_t)ctrl->registers + NVME_REG_CC
    ) = cc & ~NVME_CC_EN;

    kprintf("NVME: Waiting for controller reset...\n");

    // Wait until the controller is no longer ready.
    if (!nvme_wait_ready(
            ctrl->registers, false, ctrl->timeout_ms)) {
        kprintf("NVME: Controller reset timed out.\n");
        return false;
    }

    kprintf("NVME: Controller reset successful.\n");

    return true;
}

// Allocate and initialise an NVMe queue pair.
static bool nvme_allocate_queue(nvme_queue_t *queue,
                                uint16_t queue_id,
                                uint16_t entry_count)
{
    // Allocate the submission queue.
    if (!dma_alloc(entry_count * sizeof(nvme_command_t),
                   &queue->submission)) {
        kprintf("NVME: Submission queue allocation failed.\n");
        return false;
    }

    // Allocate the completion queue.
    if (!dma_alloc(entry_count * sizeof(nvme_completion_t),
                   &queue->completion)) {
        kprintf("NVME: Completion queue allocation failed.\n");

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

// Configure the administration queues while the controller is disabled.
static bool nvme_configure_admin_queues(nvme_controller_t *ctrl)
{
    // Queue sizes are zero-based in the AQA register.
    const uint32_t queue_size =
        ctrl->admin_queue.entry_count - 1;

    const uint32_t aqa =
        (queue_size << NVME_AQA_ACQS_SHIFT) |
        queue_size;

    // Configure the administration queue sizes.
    *(volatile uint32_t *)(
        (uint64_t)ctrl->registers + NVME_REG_AQA
    ) = aqa;

    // Configure the submission queue physical address.
    *(volatile uint64_t *)(
        (uint64_t)ctrl->registers + NVME_REG_ASQ
    ) = ctrl->admin_queue.submission.phys_addr;

    // Configure the completion queue physical address.
    *(volatile uint64_t *)(
        (uint64_t)ctrl->registers + NVME_REG_ACQ
    ) = ctrl->admin_queue.completion.phys_addr;

    // Read back the administration queue configuration.
    const uint32_t actual_aqa =
        *(volatile uint32_t *)(
            (uint64_t)ctrl->registers + NVME_REG_AQA
        );

    const uint64_t actual_asq =
        *(volatile uint64_t *)(
            (uint64_t)ctrl->registers + NVME_REG_ASQ
        );

    const uint64_t actual_acq =
        *(volatile uint64_t *)(
            (uint64_t)ctrl->registers + NVME_REG_ACQ
        );

    // Verify that the controller accepted our configuration.
    if (actual_aqa != aqa ||
        actual_asq != ctrl->admin_queue.submission.phys_addr ||
        actual_acq != ctrl->admin_queue.completion.phys_addr) {
        kprintf("NVME: Administration queue verification failed.\n");
        return false;
    }

    kprintf("NVME: AQA: 0x%x\n", (uint64_t)actual_aqa);
    kprintf("NVME: ASQ: 0x%x\n", actual_asq);
    kprintf("NVME: ACQ: 0x%x\n", actual_acq);

    kprintf("NVME: Administration queues configured.\n");

    return true;
}

// Configure and enable the NVMe controller.
static bool nvme_enable_controller(nvme_controller_t *ctrl)
{
    // Construct the controller configuration register.
    const uint32_t cc =
        (NVME_CC_CSS_NVM << NVME_CC_CSS_SHIFT) |
        (NVME_CC_MPS_4K << NVME_CC_MPS_SHIFT) |
        (NVME_CC_AMS_ROUND_ROBIN << NVME_CC_AMS_SHIFT) |
        (NVME_IOSQES_64_BYTES << NVME_CC_IOSQES_SHIFT) |
        (NVME_IOCQES_16_BYTES << NVME_CC_IOCQES_SHIFT) |
        NVME_CC_EN;

    // Apply the configuration and enable the controller.
    *(volatile uint32_t *)(
        (uint64_t)ctrl->registers + NVME_REG_CC
    ) = cc;

    kprintf("NVME: Waiting for controller readiness...\n");

    if (!nvme_wait_ready(
            ctrl->registers, true, ctrl->timeout_ms)) {
        kprintf("NVME: Controller enable timed out.\n");
        return false;
    }

    // Ensure the controller has not reported a fatal error.
    const uint32_t csts =
        *(volatile uint32_t *)(
            (uint64_t)ctrl->registers + NVME_REG_CSTS
        );

    if (csts & NVME_CSTS_CFS) {
        kprintf("NVME: Controller fatal error.\n");
        return false;
    }

    kprintf("NVME: Controller enabled successfully.\n");

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
            kprintf("NVME: Command completion timed out.\n");
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
static bool nvme_execute_command(
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
        kprintf("NVME: Unexpected command completion.\n");
        queue->failed = true;
        return false;
    }

    // Check whether the controller completed the command successfully.
    if (completion->status & NVME_COMPLETION_STATUS_MASK) {
        kprintf("NVME: Command failed. Status: 0x%x\n",
            (uint64_t)completion->status);
        return false;
    }

    return true;
}

// Mask controller interrupts for polling-based operation.
static void nvme_mask_interrupts(nvme_controller_t *ctrl)
{
    *(volatile uint32_t *)(
        (uint64_t)ctrl->registers + NVME_REG_INTMS
    ) = NVME_INTERRUPT_MASK_ALL;

    kprintf("NVME: Controller interrupts masked.\n");
}

// Retrieve the currently allocated number of I/O queues.
static bool nvme_get_number_of_queues(nvme_controller_t *ctrl)
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