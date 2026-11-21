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
#include <mem.h>
#include <vm.h>
#include <usercopy.h>
#include <abi/errno.h>

/*
 * Copies data from a user-space virtual address into a kernel buffer.
 *
 * User memory is accessed through the kernel's higher-half direct mapping,
 * after validating the page-table permissions.
 *
 * Returns zero on success or -EFAULT if any user page is inaccessible.
 */
int copy_from_user(void *destination, const void *source, size_t length)
{
    if (length == 0) {
        return 0;
    }

    if (destination == NULL || source == NULL) {
        return -EFAULT;
    }

    uint64_t user_address = (uint64_t)source;

    // Reject address overflow before accessing memory.
    if (length - 1 > UINT64_MAX - user_address) {
        return -EFAULT;
    }

    // Validate the entire range before copying anything.
    uint64_t page = user_address & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t last_page = (user_address + length - 1) & ~(uint64_t)(PAGE_SIZE - 1);

    for (;;) {
        uint64_t physical;

        if (!vm_translate_user(page, &physical)) {
            return -EFAULT;
        }

        if (page == last_page) {
            break;
        }

        page += PAGE_SIZE;
    }

    uint8_t *output = destination;
    size_t copied = 0;

    while (copied < length) {
        uint64_t address = user_address + copied;
        uint64_t physical;

        if (!vm_translate_user(address, &physical)) {
            return -EFAULT;
        }

        size_t chunk = PAGE_SIZE - (address & (PAGE_SIZE - 1));
        size_t remaining = length - copied;

        if (chunk > remaining) {
            chunk = remaining;
        }

        const uint8_t *input = (const uint8_t *)(physical + vmm_higher_half_offset);

        for (size_t i = 0; i < chunk; i++) {
            output[copied + i] = input[i];
        }

        copied += chunk;
    }

    return 0;
}