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
#include <stdbool.h>
#include <stddef.h>
#include <mem.h>
#include <vfs.h>
#include <vm.h>
#include <process.h>
#include <usermode.h>
#include <process_exec.h>
#include <thread.h>
#include <gdt.h>
#include <scheduler.h>
#include <kthread.h>
#include <kernel.h>
#include <str.h>

#include "loader.h"

#define PROCESS_EXEC_MAX_FILE_SIZE (1024 * 1024)

/*
 * Entry point for a scheduled user thread.
 *
 * The scheduler switches onto this thread's kernel stack before
 * calling this function. It then transitions into Ring 3 using
 * the process address space and the ELF entry point.
 */
static void process_thread_entry(void)
{
    thread_t *thread = scheduler_current();

    if (thread == NULL || thread->process == NULL) {
        scheduler_exit();
    }

    /*
    * Scheduled user threads currently begin execution in the kernel
    * address space. usermode_enter() is responsible for switching
    * to the process address space.
    */
    uint64_t kernel_root = vm_get_address_space();

    if (kernel_root == thread->process->page_table_root) {
        scheduler_exit();
    }

    thread->state = THREAD_RUNNING;

    usermode_enter(thread->entry_point, thread->user_stack_top,
                   thread->process->page_table_root, &thread->usermode);

    /*
    * The existing SYS_EXIT path must restore the kernel address
    * space before this thread returns to the scheduler.
    */
    if (vm_get_address_space() != kernel_root) {
        hcf();
    }

    scheduler_exit();
}

/*
 * Loads an ELF64 executable from the VFS and executes it as a
 * scheduled user thread. The calling thread waits for termination.
 */
bool process_exec(const char *path)
{
    if (path == NULL || path[0] != '/') {
        return false;
    }

    vfs_stat_t stat;

    if (vfs_stat(path, &stat) != OS_OK || stat.type != VFS_TYPE_FILE ||
        stat.size == 0 || stat.size > PROCESS_EXEC_MAX_FILE_SIZE) {
        return false;
    }

    void *buffer = malloc((size_t)stat.size);

    if (buffer == NULL) {
        return false;
    }

    vfs_file_t file = {0};

    if (vfs_open(path, VFS_OPEN_READ, &file) != OS_OK) {
        free(buffer);
        return false;
    }

    int64_t bytes_read = vfs_read(&file, buffer, (size_t)stat.size);
    vfs_close(&file);

    if (bytes_read != (int64_t)stat.size) {
        free(buffer);
        return false;
    }

    process_t *process = process_create();
    if (process == NULL) {
        free(buffer);
        return false;
    }

    thread_t *thread = thread_create(process);
    if (thread == NULL) {
        process_destroy(process);
        free(buffer);
        return false;
    }

    uint64_t entry_point = 0;
    bool success = false;

    if (!vm_create_address_space(&process->page_table_root)) {
        goto cleanup;
    }

    if (!elf_loader_load(process, buffer, (size_t)stat.size, &entry_point)) {
        goto cleanup;
    }

    thread->entry_point = entry_point;
    thread->user_stack_top = process->user_stack_pointer;

    /*
    * Reserve the upper half of the kernel stack for Ring 3 interrupts.
    * The scheduled kernel context uses the lower half.
    */
    if (!kthread_prepare(thread, process_thread_entry, thread->kernel_stack, THREAD_KERNEL_EXEC_STACK_SIZE)) {
        goto cleanup;
    }

    /*
    * The CPU enters the upper half when an interrupt arrives from Ring 3.
    */
    thread->kernel_stack_top = (uint64_t)thread->kernel_stack + THREAD_KERNEL_STACK_SIZE;

    /*
    * The calling thread must be registered before we can yield.
    */
    if (scheduler_current() == NULL) {
        goto cleanup;
    }

    uint64_t kernel_root = vm_get_address_space();

    /*
    * Register the user thread with the scheduler.
    */
    thread->state = THREAD_READY;

    if (!scheduler_add(thread)) {
        goto cleanup;
    }

    while (thread->state != THREAD_TERMINATED) {
        scheduler_yield();
    }

    /*
    * Remove the terminated thread before destroying it.
    */
    scheduler_reap();

    success = vm_get_address_space() == kernel_root;

cleanup:
    thread_destroy(thread);
    process_destroy(process);
    free(buffer);

    return success;
}