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
        kprintf("FAT32: Partition not found.\n");
        return;
    }

    fat32_volume_t volume;

    if (!fat32_init_volume(partition, &volume)) {
        kprintf("FAT32: Failed to initialise volume.\n");
        return;
    }

    const char *paths[] = {
        "/",
        "///",
        "/.",
        "/..",
        "/drivers/",
        "/drivers/.",
        "/drivers/..",
        "/drivers/nvme/..",
        "/drivers/nvme/../..",
        "/drivers/nvme/../../..",
        "/drivers//nvme/controller.c",
        "/drivers/./nvme/controller.c",
        "/drivers/nvme/../ps2.c",
        "/drivers/nvme/../../hello.txt",
        "/hello.txt/",
        "/hello.txt/.",
        "/hello.txt/..",
        "/missing/../hello.txt"
    };

    fat32_directory_record_t record;

    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        kprintf("Resolving: %s\n", paths[i]);

        fat32_lookup_status_t status = fat32_path_resolve(&volume, paths[i], &record);

        switch (status) {
            case FAT32_LOOKUP_FOUND:
                kprintf("  Found: %s\n", record.name);
                kprintf("  Cluster: %lu\n", (uint64_t)fat32_get_first_cluster(&record.entry));
                break;

            case FAT32_LOOKUP_NOT_FOUND:
                kprintf("  Path not found.\n");
                break;

            case FAT32_LOOKUP_NOT_DIRECTORY:
                kprintf("  Not a directory.\n");
                break;

            default:
                kprintf("  Lookup failed.\n");
                break;
        }
    }
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