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

static void run_test(void)
{
    partition_t *partition = partition_find("nvme0n2p1");
    fat32_volume_t volume;
    fat32_file_t file;

    if (partition == NULL || !fat32_init_volume(partition, &volume)) {
        kprintf("Failed to initialise FAT32 volume\n");
        return;
    }

    if (!fat32_file_open(&volume, "/hello.txt", &file)) {
        kprintf("Failed to open file\n");
        return;
    }

    const char original[] = "Hello Blore OS!";
    const size_t original_size = sizeof(original) - 1;
    const size_t append_size = 2500;
    const size_t expected_size = original_size + append_size;

    if (file.size != original_size) {
        kprintf("Unexpected original file size\n");
        fat32_file_close(&file);
        return;
    }

    uint32_t first_cluster = file.first_cluster;

    uint8_t *buffer = kalloc(expected_size);

    if (buffer == NULL) {
        fat32_file_close(&file);
        return;
    }

    // Generate a predictable repeating byte pattern.
    for (size_t i = 0; i < append_size; i++) {
        buffer[i] = (uint8_t)i;
    }

    int64_t position = fat32_file_seek(&file, 0, FAT32_SEEK_END);

    kprintf("Seek to EOF: %s\n", position == original_size ? "PASS" : "FAIL");

    int64_t written = fat32_file_write(&file, buffer, append_size);

    kprintf("Append across clusters: %s\n", written == append_size ? "PASS" : "FAIL");
    kprintf("Updated file size: %s\n", file.size == expected_size ? "PASS" : "FAIL");

    fat32_file_close(&file);

    if (!fat32_file_open(&volume, "/hello.txt", &file)) {
        kprintf("Failed to reopen file\n");
        kfree(buffer);
        return;
    }

    kprintf("Persisted file size: %s\n", file.size == expected_size ? "PASS" : "FAIL");
    kprintf("First cluster unchanged: %s\n",
            file.first_cluster == first_cluster ? "PASS" : "FAIL");

    memset(buffer, 0, expected_size);

    int64_t bytes_read = fat32_file_read(&file, buffer, expected_size);

    kprintf("Read entire file: %s\n", bytes_read == expected_size ? "PASS" : "FAIL");

    bool original_valid = bytes_read == expected_size &&
                          memcmp(buffer, original, original_size) == 0;

    kprintf("Original data preserved: %s\n", original_valid ? "PASS" : "FAIL");

    bool appended_valid = bytes_read == expected_size;

    if (appended_valid) {
        for (size_t i = 0; i < append_size; i++) {
            if (buffer[original_size + i] != (uint8_t)i) {
                appended_valid = false;
                break;
            }
        }
    }

    kprintf("Appended data verified: %s\n", appended_valid ? "PASS" : "FAIL");

    fat32_file_close(&file);
    kfree(buffer);
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