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

#include <stddef.h>
#include <pci.h>
#include <nvme.h>
#include <str.h>
#include <dma.h>
#include <mem.h>
#include <block.h>

#include "internal.h"

static nvme_controller_t controller;
static nvme_device_t nvme_devices[NVME_MAX_NAMESPACES];
static block_device_t block_devices[NVME_MAX_NAMESPACES];

// Display the contents of a buffer in hexadecimal.
#define NVME_HEX_BYTES_PER_LINE 16

#define NVME_TEST_NAMESPACE_ID  2
#define NVME_TEST_WRITE_LBA     1000
#define NVME_TEST_BLOCK_SIZE    512
#define NVME_TEST_WRITE_MESSAGE "Hello from BloreOS!"

/*
static void nvme_hex_dump(const uint8_t *data, size_t size)
{
    const char hex[] = "0123456789ABCDEF";

    for (size_t i = 0; i < size; i++) {
        const uint8_t value = data[i];

        nvme_log("%c%c ",
            hex[value >> 4],
            hex[value & 0x0F]);

        if ((i + 1) % NVME_HEX_BYTES_PER_LINE == 0) {
            nvme_log("\n");
        }
    }

    if (size % NVME_HEX_BYTES_PER_LINE != 0) {
        nvme_log("\n");
    }
}

// Test reading the first logical block from the namespace.
static bool nvme_test_read(nvme_controller_t *ctrl)
{
    dma_buffer_t buffer = {0};

    if (!dma_alloc(ctrl->namespace.block_size, &buffer)) {
        nvme_log("NVME: Read buffer allocation failed.\n");
        return false;
    }

    nvme_log("NVME: Reading first logical block...\n");

    const bool success = nvme_read_block(ctrl, 0, &buffer);

    if (ctrl->io_queue.failed) {
        // The controller may still be accessing this buffer.
        nvme_log("NVME: Read buffer retained for safety.\n");
        return false;
    }

    if (success) {
        nvme_log("NVME: Logical block read successful.\n");

        nvme_log("NVME: LBA 0 contents:\n");

        //nvme_hex_dump(buffer.virt_addr, ctrl->namespace.block_size);
    }

    dma_free(&buffer);

    return success;
}

// Test writing a single logical block to the data disk.
static bool nvme_test_write(nvme_controller_t *ctrl)
{
    // Ensure we are targeting our dedicated test disk.
    if (!ctrl->namespace.valid ||
        ctrl->namespace.id != NVME_TEST_NAMESPACE_ID ||
        ctrl->namespace.block_size != NVME_TEST_BLOCK_SIZE) {
        nvme_log("NVME: Incorrect test namespace.\n");
        return false;
    }

    dma_buffer_t buffer = {0};

    if (!dma_alloc(ctrl->namespace.block_size, &buffer)) {
        nvme_log("NVME: Write buffer allocation failed.\n");
        return false;
    }

    // Populate the DMA buffer with our test message.
    memmove(buffer.virt_addr,
        NVME_TEST_WRITE_MESSAGE,
        sizeof(NVME_TEST_WRITE_MESSAGE));

    nvme_log("NVME: Writing test logical block...\n");

    const bool success = nvme_write_block(
        ctrl,
        NVME_TEST_WRITE_LBA,
        &buffer
    );

    if (ctrl->io_queue.failed) {
        // The controller may still be accessing this buffer.
        nvme_log("NVME: Write buffer retained for safety.\n");
        return false;
    }

    if (success) {
        nvme_log("NVME: Logical block write successful.\n");
    }

    dma_free(&buffer);

    return success;
}
*/

static void nvme_reg_block(nvme_device_t *device, block_device_t *block)
{
    snprintf(block->name,
             sizeof(block->name),
             "nvme0n%lu",
             (uint64_t)device->namespace.id);

    block->block_size = device->namespace.block_size;
    block->block_count = device->namespace.block_count;
    block->context = device;
    block->read = nvme_block_read;
    block->write = nvme_block_write;

    if (!block_register(block)) {
        kprintf("NVME: Failed to register block device.\n");
        return;
    }

    kprintf("NVME: Registered block device '%s'.\n",
        block->name);
}

static void nvme_reg_blocks(void)
{
    // Identify and register each discovered namespace.
    for (uint32_t i = 0; i < controller.namespace_count; i++) {

        nvme_device_t *device = &nvme_devices[i];
        device->controller = &controller;

        if (!nvme_identify_namespace(
                &controller,
                controller.namespace_ids[i],
                &device->namespace)) {

            if (controller.admin_queue.failed) {
                break;
            }

            continue;
        }

        // Only register storage formats supported by our I/O layer.
        if (device->namespace.block_size > PAGE_SIZE) {
            kprintf("NVME: Unsupported namespace block size.\n");
            continue;
        }

        nvme_reg_block(device, &block_devices[i]);
    }
}

void nvme_init(void)
{
    kprintf("NVME: Initializing...\n");

    struct pci_device *dev =
        pci_find_device(NVME_PCI_CLASS, NVME_PCI_SUBCLASS);

    if (dev == NULL || dev->prog_if != NVME_PCI_PROG_IF) {
        nvme_log("NVME: Controller not found.\n");
        return;
    }

    nvme_log("NVME: Controller found.\n");

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
        nvme_log("NVME: Insufficient queue capacity.\n");
        return;
    }

    if (!nvme_allocate_queue(&controller.admin_queue,
                            NVME_ADMIN_QUEUE_ID,
                            NVME_ADMIN_QUEUE_ENTRIES)) {
        return;
    }

    nvme_log("NVME: Administration queues allocated.\n");

    nvme_log("NVME: SQ physical address: 0x%x\n",
        controller.admin_queue.submission.phys_addr);

    nvme_log("NVME: CQ physical address: 0x%x\n",
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

    if (!nvme_get_number_of_queues(&controller)) {
        return;
    }

    // Retrieve controller identification data.
    if (!nvme_identify_controller(&controller)) {
        return;
    }

    // Discover the active storage namespaces.
    if (!nvme_identify_active_namespaces(&controller)) {
        return;
    }

    // Negotiate the required number of I/O queues.
    if (!nvme_set_number_of_queues(&controller)) {
        return;
    }

    // Validate the required I/O queue capacity.
    if (controller.max_queue_entries < NVME_IO_QUEUE_ENTRIES) {
        nvme_log("NVME: Insufficient I/O queue capacity.\n");
        return;
    }

    // Allocate the I/O submission and completion queues.
    if (!nvme_allocate_queue(&controller.io_queue,
                            NVME_IO_QUEUE_ID,
                            NVME_IO_QUEUE_ENTRIES)) {
        return;
    }

    nvme_log("NVME: I/O queues allocated.\n");

    nvme_log("NVME: I/O SQ physical address: 0x%x\n",
        controller.io_queue.submission.phys_addr);

    nvme_log("NVME: I/O CQ physical address: 0x%x\n",
        controller.io_queue.completion.phys_addr);

    // Register the I/O completion queue.
    if (!nvme_create_io_completion_queue(&controller)) {
        return;
    }

    // Register the I/O submission queue.
    if (!nvme_create_io_submission_queue(&controller)) {
        return;
    }

    nvme_log("NVME: I/O queues ready.\n");

    nvme_reg_blocks();
    
    kprintf("NVME: Initialization complete.\n");
}