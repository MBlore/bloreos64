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
#include <slob.h>
#include <string.h>
#include <mem.h>
#include <alloc.h>
#include <path.h>

/*
 * Allocates and initialises a new process structure.
 *
 * The process initially has no address space or owned pages.
 * These resources are established later by the executable loader.
 *
 * The caller owns the returned process and must eventually
 * release it using process_destroy().
 */
process_t *process_create(void)
{
    process_t *process = malloc(sizeof(process_t));

    if (process == NULL) {
        return NULL;
    }

    memset(process, 0, sizeof(process_t));

    // Default process working dir.
    process->cwd[0] = '/';
    process->cwd[1] = '\0';

    return process;
}

/*
 * Destroys a process and releases all resources owned by it.
 *
 * The process must no longer be executing on any CPU, and its
 * address space must not be active when this function is called.
 */
void process_destroy(process_t *process)
{
    if (process == NULL) {
        return;
    }

    // Close any open files the process has.
    for (int fd = 3; fd < PROCESS_MAX_FILES; fd++) {
        if (process->files[fd].type != PROCESS_FD_NONE) {
            process_close(process, fd);
        }
    }

    if (process->page_table_root != 0) {
        process_free_pages(process);
        vm_destroy_address_space(process->page_table_root);
    }

    free(process);
}

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
    process_page_t *record = malloc(sizeof(process_page_t));

    if (record == NULL) {
        kfree(page);
        return false;
    }

    // Convert the HHDM pointer into a physical address for the page table.
    const uint64_t physical_address = (uint64_t)page - vmm_higher_half_offset;

    if (!vm_map_page_in(process->page_table_root, virt_addr, physical_address, flags)) {
        free(record);
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
        free(record);

        record = next;
    }

    process->owned_pages = NULL;
}

int64_t process_open(process_t *process, const char *path)
{
    if (process == NULL || path == NULL) {
        return -1;
    }

    char resolved[PROCESS_PATH_MAX];

    if (path_resolve(process->cwd, path,
                     resolved, sizeof(resolved)) != OS_OK) {
        return -1;
    }

    for (int fd = 3; fd < PROCESS_MAX_FILES; fd++) {
        process_file_t *descriptor = &process->files[fd];

        if (descriptor->type != PROCESS_FD_NONE) {
            continue;
        }

        vfs_file_t file = {0};

        if (vfs_open(resolved, VFS_OPEN_READ, &file) != OS_OK) {
            return -1;
        }

        descriptor->handle.file = file;
        descriptor->type = PROCESS_FD_FILE;

        return fd;
    }

    return -1;
}

int64_t process_opendir(process_t *process, const char *path)
{
    if (process == NULL || path == NULL) {
        return -1;
    }

    char resolved[PROCESS_PATH_MAX];

    if (path_resolve(process->cwd, path,
                     resolved, sizeof(resolved)) != OS_OK) {
        return -1;
    }

    for (int fd = 3; fd < PROCESS_MAX_FILES; fd++) {
        process_file_t *descriptor = &process->files[fd];

        if (descriptor->type != PROCESS_FD_NONE) {
            continue;
        }

        vfs_dir_t directory = {0};

        if (vfs_opendir(resolved, &directory) != OS_OK) {
            return -1;
        }

        descriptor->handle.directory = directory;
        descriptor->type = PROCESS_FD_DIRECTORY;

        return fd;
    }

    return -1;
}

int64_t process_read(process_t *process, int fd,
                     void *buffer, size_t count)
{
    if (process == NULL || buffer == NULL ||
        fd < 3 || fd >= PROCESS_MAX_FILES) {
        return -1;
    }

    process_file_t *descriptor = &process->files[fd];

    if (descriptor->type != PROCESS_FD_FILE) {
        return -1;
    }

    return vfs_read(&descriptor->handle.file, buffer, count);
}

os_error_t process_readdir(process_t *process, int fd,
                           vfs_dirent_t *entry, bool *has_entry)
{
    if (process == NULL || entry == NULL || has_entry == NULL ||
        fd < 3 || fd >= PROCESS_MAX_FILES) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    process_file_t *descriptor = &process->files[fd];

    if (descriptor->type != PROCESS_FD_DIRECTORY) {
        return OS_ERR_INVALID_ARGUMENT;
    }

    return vfs_readdir(&descriptor->handle.directory,
                       entry, has_entry);
}

int64_t process_close(process_t *process, int fd)
{
    if (process == NULL || fd < 3 || fd >= PROCESS_MAX_FILES) {
        return -1;
    }

    process_file_t *descriptor = &process->files[fd];

    switch (descriptor->type) {
        case PROCESS_FD_FILE:
            vfs_close(&descriptor->handle.file);
            break;

        case PROCESS_FD_DIRECTORY:
            vfs_closedir(&descriptor->handle.directory);
            break;

        default:
            return -1;
    }

    memset(descriptor, 0, sizeof(*descriptor));

    return 0;
}