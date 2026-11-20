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

#include "loader.h"

#define PROCESS_EXEC_MAX_FILE_SIZE (1024 * 1024)

/*
 * Loads an ELF64 executable from the VFS and runs it synchronously in Ring 3.
 *
 * The executable receives a private address space and user stack. Once it
 * exits through the current syscall mechanism, the kernel restores its
 * original address space and releases the executable's memory.
 *
 * Only one foreground user process is supported at this stage.
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

    void *buffer = kalloc((size_t)stat.size);

    if (buffer == NULL) {
        return false;
    }

    vfs_file_t file = {0};

    if (vfs_open(path, VFS_OPEN_READ, &file) != OS_OK) {
        kfree(buffer);
        return false;
    }

    int64_t bytes_read = vfs_read(&file, buffer, (size_t)stat.size);
    vfs_close(&file);

    if (bytes_read != (int64_t)stat.size) {
        kfree(buffer);
        return false;
    }

    process_t process = {0};
    uint64_t entry_point = 0;
    bool success = false;

    if (!vm_create_address_space(&process.page_table_root)) {
        goto cleanup;
    }

    if (!elf_loader_load(&process, buffer, (size_t)stat.size, &entry_point)) {
        goto cleanup;
    }

    uint64_t kernel_root = vm_get_address_space();

    usermode_enter(entry_point, process.user_stack_pointer, process.page_table_root);

    success = vm_get_address_space() == kernel_root;

cleanup:
    process_free_pages(&process);

    if (process.page_table_root != 0) {
        vm_destroy_address_space(process.page_table_root);
    }

    kfree(buffer);

    return success;
}