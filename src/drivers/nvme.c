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

    // Map the first page of the controller's register space.
    void *registers = vm_map_mmio(physical_address, PAGE_SIZE);

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
}