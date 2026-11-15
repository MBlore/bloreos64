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
#include "kernel.h"

CQueue_t *q_keyboard;

// Set the base revision to 1, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.
LIMINE_BASE_REVISION(1)


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
    
    //nvme_init();
  
    q_keyboard = cqueue_create(200);
    ps2_init();

    vm_init();

    // ---------- VMEM Mapping Testing

    // Temporary VMM hardware test.
    const uint64_t test_virtual_address = 0xFFFFC00000000000ULL;
    uint64_t test_physical_address;

    if (vm_translate(test_virtual_address, &test_physical_address)) {
        kprintf("VMM TEST: Virtual address already mapped.\n");
    } else {
        kprintf("VMM TEST: Virtual address available.\n");
    }

    // Allocate a physical page for the VMM hardware test.
    volatile uint64_t *test_page = kpalloc(1);

    if (test_page == NULL) {
        kprintf("VMM TEST: Physical allocation failed.\n");
        hcf();
    }

    // Write a known value into the allocated physical page.
    const uint64_t test_value = 0x123456789ABCDEF0ULL;
    *test_page = test_value;

    kprintf("VMM TEST: Physical page allocated.\n");

    if (*test_page == test_value) {
        kprintf("VMM TEST: Original value verified.\n");
    } else {
        kprintf("VMM TEST: Original value verification failed.\n");
        hcf();
    }

    // Obtain the physical address of the allocated page.
    const uint64_t test_page_phys =
        (uint64_t)test_page - vmm_higher_half_offset;

    // Request a writable mapping.
    const uint64_t test_mapping_flags = 1ULL << 1;

    // Map the physical page to our new virtual address.
    if (!vm_map_page(test_virtual_address,
                    test_page_phys,
                    test_mapping_flags)) {
        kprintf("VMM TEST: Mapping failed.\n");
        hcf();
    }

    kprintf("VMM TEST: Mapping created successfully.\n");

    // Verify the newly created mapping.
    if (vm_translate(test_virtual_address, &test_physical_address) &&
        test_physical_address == test_page_phys) {
        kprintf("VMM TEST: Translation verified.\n");
    } else {
        kprintf("VMM TEST: Translation failed.\n");
        hcf();
    }

    // Access the physical page through our new virtual mapping.
    volatile uint64_t *test_mapped_page =
        (volatile uint64_t *)test_virtual_address;

    // Verify that both virtual addresses reference the same data.
    if (*test_mapped_page == test_value) {
        kprintf("VMM TEST: Virtual memory read successful.\n");
    } else {
        kprintf("VMM TEST: Virtual memory read failed.\n");
        hcf();
    }

    // Write a different value through our new virtual mapping.
    const uint64_t test_new_value = 0xFEDCBA9876543210ULL;

    *test_mapped_page = test_new_value;

    // Verify the change through the original HHDM mapping.
    if (*test_page == test_new_value) {
        kprintf("VMM TEST: Virtual memory write successful.\n");
    } else {
        kprintf("VMM TEST: Virtual memory write failed.\n");
        hcf();
    }

    // ---------- VMEM Unamp Testing

    // Remove the temporary virtual memory mapping.
    if (!vm_unmap_page(test_virtual_address)) {
        kprintf("VMM TEST: Unmapping failed.\n");
        hcf();
    }

    kprintf("VMM TEST: Unmapping successful.\n");

    // Verify that the virtual address is no longer mapped.
    if (vm_translate(test_virtual_address, &test_physical_address)) {
        kprintf("VMM TEST: Mapping still exists.\n");
        hcf();
    }

    kprintf("VMM TEST: Mapping removal verified.\n");

    // Verify that the original HHDM mapping remains accessible.
    if (*test_page != test_new_value) {
        kprintf("VMM TEST: Original memory corrupted.\n");
        hcf();
    }

    kprintf("VMM TEST: Original memory preserved.\n");

    // Release the physical page through its original HHDM address.
    kfree((void *)test_page);

    kprintf("VMM TEST: Physical page released.\n");
   
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