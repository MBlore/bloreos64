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

#include <cpu.h>
#include <limine.h>
#include <str.h>
#include <atomic.h>
#include <lapic.h>

volatile struct limine_smp_request smp_request = {
    .id = LIMINE_SMP_REQUEST,
    .revision = 0,
    .flags = 0};

uint32_t bsp_lapic_id;      // Bootstrap processor APIC ID.
uint64_t cpu_count;

volatile uint64_t _cpus_awake = 1;   // The first is the BSP core.
spinlock_t _cpu_lock;

/*
 * Each CPU core starts in this function.
*/
void _cpu_awake(struct limine_smp_info *smp_info)
{
    // TODO:
    // Load the GDT.
    // Load the IDT.
    // Read up on CR3 (VMM?).
    // FPU/SSE/SSE2.
    // PAT - wrmsr(0x277
    // Sched...

    lapic_init();

    spinlock_lock(&_cpu_lock);
    kprintf("Waking up...\n");
    kprintf("LAPIC ID: %d\n", smp_info->lapic_id);
    kprintf("Processor ID: %d\n", smp_info->processor_id);

    _cpus_awake++;
    kprintf("Cores Online: %d\n", _cpus_awake);
    spinlock_unlock(&_cpu_lock);
    asm("hlt");
}

void cpu_init()
{
    bsp_lapic_id = smp_request.response->bsp_lapic_id;
    cpu_count = smp_request.response->cpu_count;

    kprintf("BSP LAPIC ID: %d\n", bsp_lapic_id);
    kprintf("CPU Count: %d\n", cpu_count);

    /*
    // Wake the cores up...
    for (uint64_t i = 0; i < cpu_count; i++) {
        if (smp_request.response->cpus[i]->lapic_id == bsp_lapic_id) {
            continue;
        }


        smp_request.response->cpus[i]->goto_address = _cpu_awake;
    }

    while (_cpus_awake < cpu_count) {
        // Wait for cores to report they are all online.
    }

    kprintf("All CPU cores online.\n");
    */
}