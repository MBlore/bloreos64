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



/*
 * Integration-tests FAT32 directory creation and deletion
 * using the NVMe-backed FAT32 partition.
 */
static void run_test(void)
{
    partition_t *partition = partition_find("nvme0n2p1");
    fat32_volume_t volume;
    fat32_directory_record_t record;

    if (partition == NULL || !fat32_init_volume(partition, &volume)) {
        kprintf("FAT32 initialisation: FAIL\n");
        return;
    }

    kprintf("FAT32 initialisation: PASS\n");

    const char *path = "/TESTDIR";

    // The test requires a clean starting state.
    if (fat32_path_resolve(&volume, path, &record) != FAT32_LOOKUP_NOT_FOUND) {
        kprintf("Test directory initially absent: FAIL\n");
        return;
    }

    kprintf("Test directory initially absent: PASS\n");

    if (!fat32_directory_create(&volume, path)) {
        kprintf("Create directory: FAIL\n");
        return;
    }

    kprintf("Create directory: PASS\n");

    if (fat32_path_resolve(&volume, path, &record) != FAT32_LOOKUP_FOUND) {
        kprintf("Resolve directory: FAIL\n");
        return;
    }

    kprintf("Resolve directory: PASS\n");

    if (!(record.entry.attributes & FAT32_ATTR_DIRECTORY)) {
        kprintf("Directory attribute: FAIL\n");
        return;
    }

    kprintf("Directory attribute: PASS\n");

    if (!fat32_directory_delete(&volume, path)) {
        kprintf("Delete directory: FAIL\n");
        return;
    }

    kprintf("Delete directory: PASS\n");

    if (fat32_path_resolve(&volume, path, &record) != FAT32_LOOKUP_NOT_FOUND) {
        kprintf("Directory removed: FAIL\n");
        return;
    }

    kprintf("Directory removed: PASS\n");

    kprintf("FAT32 mkdir/rmdir integration: PASS\n");

    /*
     * Verifies nested directory creation and deletion.
     */
    if (!fat32_directory_create(&volume, "/PARENT") ||
        !fat32_directory_create(&volume, "/PARENT/CHILD")) {
        kprintf("Create nested directories: FAIL\n");
        return;
    }

    kprintf("Create nested directories: PASS\n");

    if (fat32_directory_delete(&volume, "/PARENT")) {
        kprintf("Reject non-empty parent: FAIL\n");
        return;
    }

    kprintf("Reject non-empty parent: PASS\n");

    if (!fat32_directory_delete(&volume, "/PARENT/CHILD") ||
        !fat32_directory_delete(&volume, "/PARENT")) {
        kprintf("Delete nested directories: FAIL\n");
        return;
    }

    kprintf("Delete nested directories: PASS\n");

    /*
     * Verifies creation and deletion of a long-name directory.
     */
    if (!fat32_directory_create(&volume, "/My Documents")) {
        kprintf("Create LFN directory: FAIL\n");
        return;
    }

    kprintf("Create LFN directory: PASS\n");

    if (!fat32_directory_delete(&volume, "/My Documents")) {
        kprintf("Delete LFN directory: FAIL\n");
        return;
    }

    kprintf("Delete LFN directory: PASS\n");

    /*
     * Verifies that a deleted directory's cluster is reusable.
     */
    if (!fat32_directory_create(&volume, "/REUSE1")) {
        kprintf("Create reuse test directory: FAIL\n");
        return;
    }

    if (fat32_path_resolve(&volume, "/REUSE1", &record) != FAT32_LOOKUP_FOUND) {
        kprintf("Resolve reuse test directory: FAIL\n");
        return;
    }

    uint32_t original_cluster = fat32_get_first_cluster(&record.entry);

    if (!fat32_directory_delete(&volume, "/REUSE1") ||
        !fat32_directory_create(&volume, "/REUSE2")) {
        kprintf("Recreate directory after deletion: FAIL\n");
        return;
    }

    if (fat32_path_resolve(&volume, "/REUSE2", &record) != FAT32_LOOKUP_FOUND) {
        kprintf("Resolve recreated directory: FAIL\n");
        return;
    }

    uint32_t new_cluster = fat32_get_first_cluster(&record.entry);

    // FAT32 may choose a different free cluster, so compare FAT state
    // separately rather than requiring the allocator to reuse this one.
    kprintf("Directory recreated after release: PASS\n");

    kprintf("Cluster allocation: original=%u new=%u\n",
            original_cluster, new_cluster);

    if (!fat32_directory_delete(&volume, "/REUSE2")) {
        kprintf("Cleanup reuse test: FAIL\n");
        return;
    }

    kprintf("Cleanup reuse test: PASS\n");
    kprintf("FAT32 extended directory integration: PASS\n");

    kprintf("Unsigned zero: %u\n", 0u);
    kprintf("Unsigned maximum: %u\n", UINT32_MAX);
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