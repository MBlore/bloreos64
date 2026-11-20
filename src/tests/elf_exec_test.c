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
#include <vfs.h>
#include <mem.h>
#include <str.h>
#include <process.h>
#include <vm.h>
#include <usermode.h>

#include "../exec/loader.h"

/*
 * Reads the first userland executable from FAT32 and verifies
 * that the existing ELF loader accepts its program headers.
 */
bool elf_execution_test_run(void)
{
    const char *path = "/bin/hello";
    vfs_stat_t stat;
    vfs_file_t file;
    elf_load_plan_t plan;
    bool passed = false;

    if (vfs_stat(path, &stat) != OS_OK || stat.type != VFS_TYPE_FILE ||
        stat.size == 0 || stat.size > 1024 * 1024) {
        klogf("ELF file test: STAT FAILED\n");
        return false;
    }

    void *buffer = kalloc((size_t)stat.size);

    if (buffer == NULL) {
        klogf("ELF file test: ALLOCATION FAILED\n");
        return false;
    }

    if (vfs_open(path, VFS_OPEN_READ, &file) != OS_OK) {
        klogf("ELF file test: OPEN FAILED\n");
        kfree(buffer);
        return false;
    }

    int64_t bytes_read = vfs_read(&file, buffer, (size_t)stat.size);

    vfs_close(&file);

    if (bytes_read == (int64_t)stat.size) {
        passed = elf_loader_plan(buffer, (size_t)stat.size, &plan);
    }

    if (passed) {
        process_t process = {0};
        uint64_t entry_point = 0;

        passed = vm_create_address_space(&process.page_table_root);

        if (passed) {
            passed = elf_loader_load(&process, buffer, (size_t)stat.size, &entry_point);
        }

        if (passed) {
            passed = entry_point == plan.entry_point &&
                    process.user_stack_pointer == USER_STACK_TOP &&
                    process_find_page(&process, entry_point) != NULL &&
                    process_find_page(&process, USER_STACK_TOP - PAGE_SIZE) != NULL;
        }

        if (passed) {
            const uint64_t kernel_root = vm_get_address_space();

            klogf("ELF execution test: ENTERING /bin/hello\n");

            usermode_enter(entry_point, process.user_stack_pointer, process.page_table_root);

            passed = vm_get_address_space() == kernel_root;

            klogf("ELF execution test: %s\n", passed ? "PASS" : "CR3 RESTORE FAILED");
        }

        process_free_pages(&process);

        if (process.page_table_root != 0) {
            vm_destroy_address_space(process.page_table_root);
        }
    }

    klogf("ELF file test: %s\n", passed ? "PASS" : "FAIL");

    if (passed) {
        klogf("ELF entry point: 0x%X\n", plan.entry_point);
        klogf("ELF load segments: %u\n", (unsigned int)plan.segment_count);
    }

    kfree(buffer);

    return passed;
}