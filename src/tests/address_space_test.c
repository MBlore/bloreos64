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
#include <mem.h>
#include <vm.h>
#include <str.h>
#include <kernel.h>
#include <process.h>
#include <usermode.h>
#include <thread.h>

#include "tests.h"

/*
 * Verifies that the kernel can switch to a new address space
 * and restore its original CR3.
 */
bool address_space_test_run(void)
{
    const uint64_t kernel_root = vm_get_address_space();
    uint64_t process_root = 0;

    if (!vm_create_address_space(&process_root)) {
        klogf("Address space test: CREATE FAILED\n");
        return false;
    }

    if (!vm_switch_address_space(process_root)) {
        klogf("Address space test: SWITCH FAILED\n");
        vm_destroy_address_space(process_root);
        return false;
    }

    const bool switched = vm_get_address_space() == process_root;

    // Restore the kernel address space before doing anything else.
    if (!vm_switch_address_space(kernel_root)) {
        klogf("Address space test: RESTORE FAILED\n");
        hcf();
    }

    const bool restored = vm_get_address_space() == kernel_root;

    if (!vm_destroy_address_space(process_root)) {
        klogf("Address space test: DESTROY FAILED\n");
        return false;
    }

    if (!switched || !restored) {
        klogf("Address space test: FAILED\n");
        return false;
    }

    klogf("Address space test: PASS\n");
    return true;
}

/*
 * Verifies that a process-owned page is accessible through its own
 * address space and is not mapped at the same address in the kernel.
 */
bool address_space_memory_test_run(void)
{
    const uint64_t kernel_root = vm_get_address_space();
    const uint64_t user_address = 0x500000ULL;
    const uint64_t test_value = 0x123456789ABCDEF0ULL;

    process_t process = {0};
    uint64_t physical_address = 0;
    bool passed = false;

    if (!vm_create_address_space(&process.page_table_root)) {
        klogf("Process memory test: CREATE FAILED\n");
        return false;
    }

    if (!process_alloc_page(&process, user_address, PAGE_USER | PAGE_RW | PAGE_NX)) {
        klogf("Process memory test: ALLOCATION FAILED\n");
        goto cleanup;
    }

    uint64_t *page = process_find_page(&process, user_address);

    if (page == NULL) {
        klogf("Process memory test: PAGE NOT FOUND\n");
        goto cleanup;
    }

    *page = test_value;

    if (!vm_switch_address_space(process.page_table_root)) {
        klogf("Process memory test: SWITCH FAILED\n");
        goto cleanup;
    }

    const bool translated = vm_translate(user_address, &physical_address);
    const bool value_matches = translated &&
        *(uint64_t *)(physical_address + vmm_higher_half_offset) == test_value;

    if (!vm_switch_address_space(kernel_root)) {
        klogf("Process memory test: RESTORE FAILED\n");
        hcf();
    }

    const bool isolated = !vm_translate(user_address, &physical_address);

    passed = translated && value_matches && isolated;

cleanup:
    process_free_pages(&process);
    vm_destroy_address_space(process.page_table_root);

    klogf("Process memory test: %s\n", passed ? "PASS" : "FAIL");

    return passed;
}

/*
 * Verifies that Ring 3 execution returns to the original kernel
 * address space after the user program invokes its exit syscall.
 */
bool address_space_usermode_test_run(void)
{
    const uint64_t kernel_root = vm_get_address_space();

    klogf("User-mode transition test: ENTERING\n");

    usermode_context_t usermode = {0};
    usermode_enter(USER_CODE_BASE, USER_STACK_TOP, kernel_root, &usermode);

    if (vm_get_address_space() != kernel_root) {
        klogf("User-mode transition test: CR3 RESTORE FAILED\n");
        return false;
    }

    klogf("User-mode transition test: PASS\n");
    return true;
}