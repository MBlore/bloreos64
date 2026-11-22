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
#include <str.h>
#include <usermode.h>
#include <mem.h>
#include <vm.h>
#include <terminal.h>
#include <abi/syscall.h>
#include <abi/errno.h>
#include <abi/unistd.h>
#include <usercopy.h>
#include <thread.h>

/*
 * Writes bytes from a user-space buffer to stdout or stderr.
 *
 * Validates the complete user-memory range before producing output.
 * Copies data through a kernel-owned staging buffer to avoid directly
 * accessing user memory from the terminal renderer.
 */
static int64_t syscall_write(uint64_t fd, uint64_t user_buffer, uint64_t count)
{
    if (fd != STDOUT_FILENO && fd != STDERR_FILENO) {
        return -EBADF;
    }

    if (count == 0) {
        return 0;
    }

    if (count > INT64_MAX || count - 1 > UINT64_MAX - user_buffer) {
        return -EINVAL;
    }

    // Validate every page before writing any output.
    uint64_t page = user_buffer & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t last_page = (user_buffer + count - 1) & ~(uint64_t)(PAGE_SIZE - 1);

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

    // A small kernel stack buffer is sufficient for streaming output.
    char buffer[256];
    uint64_t written = 0;

    while (written < count) {
        size_t length = (size_t)(count - written);

        if (length > sizeof(buffer)) {
            length = sizeof(buffer);
        }

        int result = copy_from_user(buffer, (const void *)(user_buffer + written), length);

        if (result != 0) {
            return written != 0 ? (int64_t)written : result;
        }

        term_write(buffer, length);
        written += length;
    }

    return (int64_t)written;
}

/*
 * Dispatches system calls requested through interrupt 0x80.
 *
 * Arguments are supplied by the assembly interrupt handler.
 * Only Ring 3 callers are accepted.
 */
uint64_t syscall_dispatch(uint64_t number, uint64_t arg1, uint64_t arg2,
                          uint64_t arg3, uint64_t caller_cpl)
{
    if (caller_cpl != 3) {
        return (uint64_t)-EPERM;
    }

    switch (number) {
        case SYS_WRITE:
            return (uint64_t)syscall_write(arg1, arg2, arg3);

        case SYS_EXIT:
            usermode_exit();
            __builtin_unreachable();

        default:
            return (uint64_t)-ENOSYS;
    }
}