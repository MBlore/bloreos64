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
#include <stddef.h>
#include <stdbool.h>

#include <limine.h>
#include <serial.h>
#include <str.h>
#include <cpu.h>
#include <cpuid.h>
#include <mem.h>
#include <slob.h>
#include <alloc.h>
#include <vm.h>
#include <gdt.h>
#include <idt.h>
#include <ps2.h>
#include <lapic.h>
#include <acpi.h>
#include <ioapic.h>
#include <terminal.h>
#include <queue.h>
#include <hpet.h>
#include <pit.h>
#include <pci.h>
#include <nvme.h>
#include <dma.h>
#include <block.h>
#include <gpt.h>
#include <partition.h>
#include <fat32.h>
#include <crc32.h>

#include "kernel.h"

CQueue_t *q_keyboard;

// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.
LIMINE_BASE_REVISION(1)

void run_test(void)
{
    partition_t *partition = partition_find("nvme0n2p1");

    if (partition == NULL) {
        kprintf("Partition not found\n");
        return;
    }

    fat32_volume_t volume;

    if (!fat32_init_volume(partition, &volume)) {
        kprintf("Failed to initialise FAT32 volume\n");
        return;
    }

    fat32_file_t file;

    if (!fat32_file_open(&volume, "/drivers/nvme/controller.c", &file)) {
        kprintf("Failed to open file\n");
        return;
    }

    // Load the complete file as a reference for random-access reads.
    uint8_t *reference = kalloc(file.size);

    if (reference == NULL) {
        fat32_file_close(&file);
        return;
    }

    if (fat32_file_read(&file, reference, file.size) != (int64_t)file.size) {
        kprintf("Failed to read reference data\n");
        kfree(reference);
        fat32_file_close(&file);
        return;
    }

    uint8_t buffer[512];
    int64_t position;
    int64_t bytes_read;

    // Seek forwards from the beginning of the file.
    position = fat32_file_seek(&file, 900, FAT32_SEEK_SET);
    bytes_read = fat32_file_read(&file, buffer, sizeof(buffer));

    kprintf("SEEK_SET: %s\n",
            position == 900 && bytes_read == sizeof(buffer) &&
            memcmp(buffer, reference + 900, sizeof(buffer)) == 0 ? "PASS" : "FAIL");

    // Seek backwards relative to the current position.
    position = fat32_file_seek(&file, -1000, FAT32_SEEK_CUR);
    bytes_read = fat32_file_read(&file, buffer, sizeof(buffer));

    kprintf("SEEK_CUR: %s\n",
            position == 412 && bytes_read == sizeof(buffer) &&
            memcmp(buffer, reference + 412, sizeof(buffer)) == 0 ? "PASS" : "FAIL");

    // Seek backwards from EOF.
    position = fat32_file_seek(&file, -100, FAT32_SEEK_END);
    bytes_read = fat32_file_read(&file, buffer, sizeof(buffer));

    kprintf("SEEK_END: %s\n",
            position == (int64_t)file.size - 100 && bytes_read == 100 &&
            memcmp(buffer, reference + file.size - 100, 100) == 0 ? "PASS" : "FAIL");

    // Seeking beyond EOF must not extend the file.
    position = fat32_file_seek(&file, 100, FAT32_SEEK_END);
    bytes_read = fat32_file_read(&file, buffer, sizeof(buffer));

    kprintf("Beyond EOF: %s\n",
            position == (int64_t)file.size + 100 && bytes_read == 0 ? "PASS" : "FAIL");

    // Invalid seeks must preserve the existing file position.
    uint64_t previous_position = file.position;

    position = fat32_file_seek(&file, -1, FAT32_SEEK_SET);

    kprintf("Negative position: %s\n",
            position == -1 && file.position == previous_position ? "PASS" : "FAIL");

    position = fat32_file_seek(&file, INT64_MAX, FAT32_SEEK_CUR);

    kprintf("Integer overflow: %s\n",
            position == -1 && file.position == previous_position ? "PASS" : "FAIL");

    kfree(reference);
    fat32_file_close(&file);
}

void kernel_main(void)
{
    // Ensure the bootloader actually understands our base revision (see spec).
    if (LIMINE_BASE_REVISION_SUPPORTED == false) {
        hcf();
    }

    init_serial(PORT_COM1);
    term_init();

    term_fgcolor(0x00FF00);
    kprintf("BloreOS Alpha v0.01\n\n");
    term_fgcolor(TERM_DEFAULT_FGCOLOR);

    report_cpu_details();

    disable_interrupts();

    init_gdt();
    idt_init();
    pit_disable_timer();

    enable_interrupts();

    kprintf("GDT/IDT initialized.\n");

    kmem_init();

    kprintf("PMM Available Pages: %lu\n", num_pages_available);

    acpi_init();
    
    hpet_init();

    cpu_init();

    lapic_init();

    pci_init();
     
    q_keyboard = cqueue_create(200);
    ps2_init();

    vm_init();

    nvme_init();

    // Discover partitions on all registered block devices.
    for (uint32_t i = 0; i < block_get_count(); i++) {
        block_device_t *device = block_get_device(i);

        if (device != NULL) {
            gpt_discover_partitions(device);
        }
    }

    run_test();
   
    // Process pending events or idle until an interrupt occurs.
    while(1) {
        disable_interrupts();

        // If there are no pending events, enter the CPU idle state.
        // Calling 'sti' here enables interrupts right before halting which solves
        // an interrupt race condition.
        if (q_keyboard->num_items == 0) {
            __asm__ volatile("sti\n\thlt" ::: "memory");
            continue;
        }

        uint32_t scanCode = cqueue_read(q_keyboard);

        enable_interrupts();

        if (scanCode >= 256) {
            continue;
        }

        KeyEvent_t *pKE = scancode_map[scanCode];
        if (pKE != NULL) {
            term_keyevent(pKE);
        }
    }

    hcf();
}

void report_cpu_details()
{
    char vendor[13] = {0};
    get_cpu_vendor(vendor);
    kprintf("CPU Vendor: %s\n", vendor);

    char brand[49] = {0};
    get_cpu_brand(brand);
    kprintf("CPU Brand: %s\n", brand);

    uint32_t logicalProcessorsPerCore, totalLogicalProcessors;
    get_cpu_topology(&logicalProcessorsPerCore, &totalLogicalProcessors);

    kprintf("Logical Processors Per Core: %d\n", logicalProcessorsPerCore);
    kprintf("Total Logical Processors: %d\n", totalLogicalProcessors);

    uint32_t baseFrequencyMHz, maxFrequencyMHz, busFrequencyMHz;
    get_cpu_frequency(&baseFrequencyMHz, &maxFrequencyMHz, &busFrequencyMHz);

    if (baseFrequencyMHz == 0 && maxFrequencyMHz == 0 && busFrequencyMHz == 0) {
        kprintf("CPU Frequency (0x16) not supported.\n");
    }
    else {
        kprintf("Base Frequency: %d\n", baseFrequencyMHz);
        kprintf("Max Frequency: %d\n", maxFrequencyMHz);
        kprintf("Bus Frequency: %d\n", busFrequencyMHz);
    }
}