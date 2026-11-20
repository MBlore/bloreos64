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
#include <usermode.h>

static void *user_code_page;
static void *user_stack_pages;

/*
 * Initial Ring 3 test program.
 *
 * mov eax, 1
 * int 0x80
 * mov eax, 2
 * int 0x80
 * jmp $
 */
static const uint8_t user_test_program[] = {
    0xB8, 0x01, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xB8, 0x02, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xEB, 0xFE
};

/*
 * Allocates and maps the initial Ring 3 code and stack regions.
 *
 * The physical pages remain accessible to the kernel through the HHDM,
 * while the lower-half virtual addresses are intended for user execution.
 */
bool usermode_prepare(void)
{
    user_code_page = kpalloc(1);

    if (user_code_page == NULL) {
        return false;
    }

    user_stack_pages = kpalloc(USER_STACK_SIZE / PAGE_SIZE);

    if (user_stack_pages == NULL) {
        kfree(user_code_page);
        user_code_page = NULL;
        return false;
    }

    memset(user_code_page, 0, PAGE_SIZE);
    memset(user_stack_pages, 0, USER_STACK_SIZE);

    memcpy(user_code_page, user_test_program, sizeof(user_test_program));

    uint64_t code_phys = (uint64_t)user_code_page - vmm_higher_half_offset;
    uint64_t stack_phys = (uint64_t)user_stack_pages - vmm_higher_half_offset;
    uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    uint64_t mapped_stack_pages = 0;

    if (!vm_map_user_page(USER_CODE_BASE, code_phys, false)) {
        goto failed;
    }

    for (uint64_t i = 0; i < USER_STACK_SIZE / PAGE_SIZE; i++) {
        uint64_t virt = stack_base + i * PAGE_SIZE;
        uint64_t phys = stack_phys + i * PAGE_SIZE;

        if (!vm_map_user_page(virt, phys, true)) {
            goto failed;
        }

        mapped_stack_pages++;
    }

    klogf("User code mapped at 0x400000\n");
    klogf("User stack mapped below 0x800000\n");

    return true;

failed:
    for (uint64_t i = 0; i < mapped_stack_pages; i++) {
        vm_unmap_page(stack_base + i * PAGE_SIZE);
    }

    vm_unmap_page(USER_CODE_BASE);

    kfree(user_stack_pages);
    kfree(user_code_page);

    user_stack_pages = NULL;
    user_code_page = NULL;

    return false;
}