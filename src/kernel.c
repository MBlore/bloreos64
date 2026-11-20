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
#include <vfs.h>
#include <error.h>
#include <time.h>
#include <usermode.h>
#include <process.h>
#include <kthread.h>
#include <thread.h>
#include <scheduler.h>

#include "tests/tests.h"

#include "kernel.h"

static thread_t main_thread;

static fat32_volume_t root_volume;
static fat32_volume_t data_volume;

CQueue_t *q_keyboard;

// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.
LIMINE_BASE_REVISION(1)

/*
 * Ensures that a directory exists at the supplied VFS path.
 *
 * Creates missing directories and rejects existing non-directory objects.
 * Parent directories must already exist.
 */
static os_error_t ensure_directory(const char *path)
{
    vfs_stat_t stat;
    os_error_t result = vfs_stat(path, &stat);

    if (result == OS_OK) {
        return stat.type == VFS_TYPE_DIRECTORY ? OS_OK : OS_ERR_NOT_DIRECTORY;
    }

    if (result != OS_ERR_NOT_FOUND) {
        return result;
    }

    return vfs_mkdir(path);
}

/*
 * Initializes and mounts the secondary FAT32 data partition.
 *
 * Creates the mount-point directories if necessary and preserves
 * any errors returned by filesystem initialization or mounting.
 */
static os_error_t init_data_filesystem(void)
{
    partition_t *partition = partition_find("nvme0n2p1");

    if (partition == NULL) {
        return OS_ERR_NOT_FOUND;
    }

    os_error_t result = fat32_init_volume(partition, &data_volume);

    if (result != OS_OK) {
        return result;
    }

    result = ensure_directory("/volumes");

    if (result != OS_OK) {
        return result;
    }

    result = ensure_directory("/volumes/data");

    if (result != OS_OK) {
        return result;
    }

    return vfs_mount("/volumes/data", &fat32_vfs_driver, &data_volume);
}

/*
 * Initialises the root FAT32 filesystem and registers it with VFS.
 */
static os_error_t init_root_filesystem(void)
{
    partition_t *partition = partition_find("nvme0n1p1");

    if (partition == NULL) {
        return OS_ERR_NOT_FOUND;
    }

    os_error_t result = fat32_init_volume(partition, &root_volume);

    if (result != OS_OK) {
        kprintf("FAT32: nvme0n1p1 initialization failed: %s\n", os_error_string(result));
        return result;
    }

    vfs_init();

    return vfs_mount("/", &fat32_vfs_driver, &root_volume);
}

/*
 * Initialises cooperative scheduling and registers the bootstrap kernel thread.
 */
static void init_kernel_scheduler(void)
{
    scheduler_init();

    memset(&main_thread, 0, sizeof(main_thread));

    main_thread.tid = 0;
    main_thread.state = THREAD_READY;

    if (!scheduler_add(&main_thread)) {
        klogf("Failed to register main kernel thread\n");
        hcf();
    }

    main_thread.state = THREAD_RUNNING;
    scheduler_set_current(&main_thread);

    klogf("Scheduler: current thread = %lu\n", scheduler_current()->tid);
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
    klogf("BloreOS Alpha v0.01\n\n");
    term_fgcolor(TERM_DEFAULT_FGCOLOR);
    klogf("Initializing...\n");

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

    if (time_init()) {
        rtc_datetime_t now;

        if (time_get_datetime(&now)) {
            kprintf("System clock initialized: %u-%u-%u %u:%u:%u UTC\n",
                    (unsigned int)now.year, (unsigned int)now.month, (unsigned int)now.day,
                    (unsigned int)now.hour, (unsigned int)now.minute, (unsigned int)now.second);
        }
    } else {
        kprintf("System clock: RTC initialization failed\n");
    }

    cpu_init();

    lapic_init();

    pci_init();
     
    q_keyboard = cqueue_create(200);
    ps2_init();

    vm_init();

    scheduler_init();

    init_kernel_scheduler();

    //scheduler_test_run();
    //address_space_test_run();
    //address_space_memory_test_run();
    //address_space_usermode_test_run();

    nvme_init();

    // Discover partitions on all registered block devices.
    for (uint32_t i = 0; i < block_get_count(); i++) {
        block_device_t *device = block_get_device(i);

        if (device != NULL) {
            gpt_discover_partitions(device);
        }
    }

    os_error_t result = init_root_filesystem();

    if (result == OS_OK) {
        os_error_t result = init_data_filesystem();

        if (result == OS_OK) {
            kprintf("Data filesystem mounted at /volumes/data\n");
        } else {
            kprintf("Data filesystem: %s\n", os_error_string(result));
        }
    }
    
    elf_execution_test_run();

    klogf("System ready. Type 'help' for commands.\n\n");

    term_show_prompt();
   
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