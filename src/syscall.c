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
#include <scheduler.h>
#include <process.h>

#define SYSCALL_MAX_PATH 256

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
 * Opens a file requested by a Ring 3 process.
 *
 * Copies a null-terminated pathname from user memory into a kernel-owned
 * buffer before passing it to the process file descriptor layer.
 *
 * Parameters:
 *   user_path - Virtual address of the pathname in user memory.
 *   flags     - Open flags. Must currently be zero.
 *
 * Returns:
 *   A file descriptor (>= 3) on success, or a negative errno value
 *   on failure.
 */
static int64_t syscall_open(uint64_t user_path, uint64_t flags)
{
    // Read-only files for now.
    if (flags != 0) {
        return -EINVAL;
    }

    thread_t *thread = scheduler_current();

    if (thread == NULL || thread->process == NULL) {
        return -EPERM;
    }

    if (user_path == 0) {
        return -EFAULT;
    }

    char path[SYSCALL_MAX_PATH];

    for (size_t i = 0; i < sizeof(path); i++) {
        if (user_path > UINT64_MAX - i) {
            return -EFAULT;
        }

        int result = copy_from_user(
            &path[i],
            (const void *)(user_path + i),
            1
        );

        if (result != 0) {
            return result;
        }

        if (path[i] == '\0') {
            if (i == 0) {
                return -EINVAL;
            }

            int64_t fd = process_open(thread->process, path);

            return fd < 0 ? -ENOENT : fd;
        }
    }

    // Path is too long or not null-terminated.
    return -EINVAL;
}

/*
 * Reads bytes from an open file into a Ring 3 process buffer.
 *
 * Validates the user destination before performing file I/O.
 * Reads through a small kernel-owned staging buffer and copies
 * the results into writable user-space memory.
 *
 * Parameters:
 *   fd          - File descriptor belonging to the calling process.
 *   user_buffer - Virtual address of the destination in user memory.
 *   count       - Maximum number of bytes to read.
 *
 * Returns:
 *   Number of bytes read, zero at EOF, or a negative errno value.
 */
static int64_t syscall_read(uint64_t fd, uint64_t user_buffer,
                            uint64_t count)
{
    thread_t *thread = scheduler_current();

    if (thread == NULL || thread->process == NULL) {
        return -EPERM;
    }

    if (fd < 3 || fd >= PROCESS_MAX_FILES ||
        thread->process->files[fd].type == PROCESS_FD_NONE) {
        return -EBADF;
    }

    if (count == 0) {
        return 0;
    }

    if (count > INT64_MAX ||
        count - 1 > UINT64_MAX - user_buffer) {
        return -EINVAL;
    }

    // Validate the entire destination before reading the file.
    uint64_t page = user_buffer & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t last_page =
        (user_buffer + count - 1) & ~(uint64_t)(PAGE_SIZE - 1);

    for (;;) {
        uint64_t physical;

        if (!vm_translate_user_write(page, &physical)) {
            return -EFAULT;
        }

        if (page == last_page) {
            break;
        }

        page += PAGE_SIZE;
    }

    char buffer[256];
    uint64_t total = 0;

    while (total < count) {
        size_t length = (size_t)(count - total);

        if (length > sizeof(buffer)) {
            length = sizeof(buffer);
        }

        int64_t bytes_read = process_read(
            thread->process, (int)fd, buffer, length
        );

        if (bytes_read < 0) {
            return total != 0 ? (int64_t)total : -EIO;
        }

        if (bytes_read == 0) {
            break;
        }

        int result = copy_to_user(
            (void *)(user_buffer + total),
            buffer,
            (size_t)bytes_read
        );

        if (result != 0) {
            return total != 0 ? (int64_t)total : result;
        }

        total += (uint64_t)bytes_read;

        // A short read is allowed; return what we received.
        if ((size_t)bytes_read < length) {
            break;
        }
    }

    return (int64_t)total;
}

/*
 * Closes a file descriptor belonging to the calling Ring 3 process.
 *
 * Releases the underlying VFS file handle and makes the descriptor
 * available for reuse.
 *
 * Parameters:
 *   fd - File descriptor to close.
 *
 * Returns:
 *   Zero on success, or a negative errno value on failure.
 */
static int64_t syscall_close(uint64_t fd)
{
    thread_t *thread = scheduler_current();

    if (thread == NULL || thread->process == NULL) {
        return -EPERM;
    }

    if (fd < 3 || fd >= PROCESS_MAX_FILES) {
        return -EBADF;
    }

    if (process_close(thread->process, (int)fd) != 0) {
        return -EBADF;
    }

    return 0;
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

        case SYS_OPEN:
            return (uint64_t)syscall_open(arg1, arg2);

        case SYS_READ:
            return (uint64_t)syscall_read(arg1, arg2, arg3);

        case SYS_CLOSE:
            return (uint64_t)syscall_close(arg1);

        default:
            return (uint64_t)-ENOSYS;
    }
}