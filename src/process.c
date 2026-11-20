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
#include <process.h>
#include <thread.h>
#include <vm.h>
#include <string.h>
#include <mem.h>

/*
 * Allocates a zero-initialised physical page, maps it into the process
 * address space and records ownership for subsequent cleanup.
 *
 * This routine must not be used to map shared or externally owned pages.
 */
bool process_alloc_page(process_t *process, uint64_t virt_addr, uint64_t flags)
{
    if (process == NULL || process->page_table_root == 0) {
        return false;
    }

    if (!(flags & PAGE_USER)) {
        return false;
    }

    // Allocate a physical page for the process.
    void *page = kpalloc(1);

    if (page == NULL) {
        return false;
    }

    memset(page, 0, PAGE_SIZE);

    // Allocate a separate record to track ownership.
    process_page_t *record = kalloc(sizeof(process_page_t));

    if (record == NULL) {
        kfree(page);
        return false;
    }

    // Convert the HHDM pointer into a physical address for the page table.
    const uint64_t physical_address = (uint64_t)page - vmm_higher_half_offset;

    if (!vm_map_page_in(process->page_table_root, virt_addr, physical_address, flags)) {
        kfree(record);
        kfree(page);
        return false;
    }

    record->virtual_address = virt_addr;
    record->physical_page = page;
    record->next = process->owned_pages;

    process->owned_pages = record;

    return true;
}

/*
 * Finds a physical page owned by a process at the specified virtual address.
 *
 * The returned pointer is the higher-half mapping of the physical page,
 * allowing the kernel to access it without switching CR3.
 *
 * The virtual address may point anywhere within the page.
 * Returns NULL if no matching page is owned by the process.
 */
void *process_find_page(const process_t *process, uint64_t virt_addr)
{
    if (process == NULL) {
        return NULL;
    }

    const uint64_t page_base = virt_addr & ~(PAGE_SIZE - 1ULL);

    for (process_page_t *record = process->owned_pages; record != NULL; record = record->next) {
        if (record->virtual_address == page_base) {
            return record->physical_page;
        }
    }

    return NULL;
}

/*
 * Releases all physical pages owned by a process.
 *
 * The process must be inactive on every CPU. Its address space must
 * remain valid until this function completes.
 *
 * This function also releases the page ownership records.
 */
void process_free_pages(process_t *process)
{
    if (process == NULL) {
        return;
    }

    process_page_t *record = process->owned_pages;

    while (record != NULL) {
        process_page_t *next = record->next;

        // Remove the mapping before releasing its physical backing page.
        vm_unmap_page_in(process->page_table_root, record->virtual_address);

        kfree(record->physical_page);
        kfree(record);

        record = next;
    }

    process->owned_pages = NULL;
}

/*
 * Initialises the bootstrap user process and its first thread.
 *
 * This is temporary single-process plumbing. Process allocation,
 * lifetime management and scheduling will be introduced separately.
 */
void process_init_test(process_t *process, thread_t *thread)
{
    process->pid = 1;
    process->page_table_root = 0;

    thread->tid = 1;
    thread->process = process;
    thread->state = THREAD_READY;
    thread->kernel_stack_top = 0;
    thread->user_stack_top = USER_STACK_TOP;
    thread->entry_point = USER_CODE_BASE;

    thread->context.rsp = 0;
    thread->context.rbx = 0;
    thread->context.rbp = 0;
    thread->context.r12 = 0;
    thread->context.r13 = 0;
    thread->context.r14 = 0;
    thread->context.r15 = 0;
}