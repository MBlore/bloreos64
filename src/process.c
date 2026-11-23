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
#include <abi/fcntl.h>
#include <abi/errno.h>
#include <abi/dirent.h>

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

        descriptor->handle.directory.handle = directory;
        descriptor->handle.directory.has_pending_entry = false;
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

    return vfs_readdir(&descriptor->handle.directory.handle,
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
            vfs_closedir(&descriptor->handle.directory.handle);
            break;

        default:
            return -1;
    }

    memset(descriptor, 0, sizeof(*descriptor));

    return 0;
}

/*
 * Opens a file or directory and assigns a process-owned descriptor.
 *
 * Currently supports AT_FDCWD and read-only access.
 * Returns a descriptor on success or negative errno on failure.
 */
int64_t process_openat(process_t *process, int dirfd,
                       const char *path, int flags)
{
    if (process == NULL || path == NULL || path[0] == '\0') {
        return -EINVAL;
    }

    if ((flags & ~(O_ACCMODE | O_DIRECTORY)) != 0 ||
        (flags & O_ACCMODE) != O_RDONLY) {
        return -EINVAL;
    }

    if (dirfd != AT_FDCWD && path[0] != '/') {
        return -ENOTSUP;
    }

    char resolved[PROCESS_PATH_MAX];

    os_error_t result = path_resolve(
        process->cwd, path, resolved, sizeof(resolved)
    );

    if (result == OS_ERR_NAME_TOO_LONG) {
        return -ENAMETOOLONG;
    }

    if (result != OS_OK) {
        return -EINVAL;
    }

    int fd = -1;

    for (int i = 3; i < PROCESS_MAX_FILES; i++) {
        if (process->files[i].type == PROCESS_FD_NONE) {
            fd = i;
            break;
        }
    }

    if (fd < 0) {
        return -EMFILE;
    }

    vfs_stat_t stat;

    result = vfs_stat(resolved, &stat);

    switch (result) {
        case OS_OK:
            break;
        case OS_ERR_NOT_FOUND:
            return -ENOENT;
        case OS_ERR_NOT_DIRECTORY:
            return -ENOTDIR;
        case OS_ERR_ACCESS_DENIED:
            return -EACCES;
        default:
            return -EIO;
    }

    process_file_t *descriptor = &process->files[fd];

    if (stat.type == VFS_TYPE_DIRECTORY) {
        vfs_dir_t directory = {0};

        result = vfs_opendir(resolved, &directory);

        if (result != OS_OK) {
            return -EIO;
        }

        descriptor->handle.directory.handle = directory;
        descriptor->handle.directory.has_pending_entry = false;
        descriptor->type = PROCESS_FD_DIRECTORY;
    } else {
        if (flags & O_DIRECTORY) {
            return -ENOTDIR;
        }

        vfs_file_t file = {0};

        result = vfs_open(resolved, VFS_OPEN_READ, &file);

        if (result != OS_OK) {
            return -EIO;
        }

        descriptor->handle.file = file;
        descriptor->type = PROCESS_FD_FILE;
    }

    return fd;
}


/*
 * Packs directory entries into Linux-compatible dirent64 records.
 *
 * The destination is a kernel-owned buffer, not a user pointer.
 * A pending entry is retained if the next record cannot fit.
 *
 * Returns bytes written, zero at EOF, or negative errno.
 */
int64_t process_getdents64(process_t *process, int fd,
                           void *buffer, size_t count)
{
    if (process == NULL) {
        return -EINVAL;
    }

    if (fd < 3 || fd >= PROCESS_MAX_FILES) {
        return -EBADF;
    }

    process_file_t *descriptor = &process->files[fd];

    if (descriptor->type == PROCESS_FD_NONE) {
        return -EBADF;
    }

    if (descriptor->type != PROCESS_FD_DIRECTORY) {
        return -ENOTDIR;
    }

    if (buffer == NULL) {
        return -EFAULT;
    }

    process_directory_t *directory = &descriptor->handle.directory;
    size_t total = 0;

    for (;;) {
        if (!directory->has_pending_entry) {
            bool has_entry = false;

            os_error_t result = vfs_readdir(
                &directory->handle,
                &directory->pending_entry,
                &has_entry
            );

            if (result != OS_OK) {
                return total != 0 ? (int64_t)total : -EIO;
            }

            if (!has_entry) {
                break;
            }

            directory->has_pending_entry = true;
        }

        vfs_dirent_t *entry = &directory->pending_entry;

        size_t name_length = 0;

        while (name_length < sizeof(entry->name) &&
               entry->name[name_length] != '\0') {
            name_length++;
        }

        if (name_length == sizeof(entry->name)) {
            return total != 0 ? (int64_t)total : -EIO;
        }

        size_t record_length = DIRENT64_RECLEN(name_length);

        if (record_length > count - total) {
            return total != 0 ? (int64_t)total : -EINVAL;
        }

        struct linux_dirent64 *record =
            (struct linux_dirent64 *)((char *)buffer + total);

        memset(record, 0, record_length);

        // Inode numbers and seek cookies are not yet exposed
        // by the VFS. Zero indicates unavailable metadata.
        record->d_ino = 0;
        record->d_off = 0;
        record->d_reclen = (uint16_t)record_length;
        record->d_type = entry->is_directory ? DT_DIR : DT_REG;

        memcpy(record->d_name, entry->name, name_length + 1);

        total += record_length;
        directory->has_pending_entry = false;
    }

    return (int64_t)total;
}
