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
#include <terminal/terminal.h>
#include <usercopy.h>
#include <thread.h>
#include <scheduler.h>
#include <process.h>
#include <path.h>
#include <string.h>

#include <abi/syscall.h>
#include <abi/errno.h>
#include <abi/unistd.h>
#include <abi/dirent.h>
#include <abi/stat.h>
#include <abi/fcntl.h>

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
 * Copies a null-terminated pathname from user memory.
 *
 * Returns zero on success or negative errno on failure.
 */
static int copy_user_path(uint64_t user_path,
                          char *path, size_t capacity)
{
    if (user_path == 0) {
        return -EFAULT;
    }

    for (size_t i = 0; i < capacity; i++) {
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
            return i == 0 ? -EINVAL : 0;
        }
    }

    return -ENAMETOOLONG;
}

static int64_t syscall_open(uint64_t user_path, uint64_t flags)
{
    if (flags != 0) {
        return -EINVAL;
    }

    thread_t *thread = scheduler_current();

    if (thread == NULL || thread->process == NULL) {
        return -EPERM;
    }

    char path[SYSCALL_MAX_PATH];

    int result = copy_user_path(
        user_path, path, sizeof(path)
    );

    if (result != 0) {
        return result;
    }

    int64_t fd = process_open(thread->process, path);

    return fd < 0 ? -ENOENT : fd;
}


/*
 * Opens a file or directory relative to a directory descriptor.
 *
 * Currently supports AT_FDCWD for relative paths.
 * Absolute paths are accepted independently of dirfd.
 *
 * Returns a descriptor or negative errno.
 */
static int64_t syscall_openat(uint64_t dirfd,
                              uint64_t user_path,
                              uint64_t flags)
{
    thread_t *thread = scheduler_current();

    if (thread == NULL || thread->process == NULL) {
        return -EPERM;
    }

    char path[SYSCALL_MAX_PATH];

    int result = copy_user_path(
        user_path, path, sizeof(path)
    );

    if (result != 0) {
        return result;
    }

    return process_openat(
        thread->process,
        (int)(int64_t)dirfd,
        path,
        (int)flags
    );
}

/*
 * Retrieves file metadata using a pathname relative to the process CWD.
 *
 * Currently supports AT_FDCWD for relative paths and absolute paths
 * independently of dirfd. Flags must be zero.
 *
 * Returns zero on success or negative errno on failure.
 */
static int64_t syscall_fstatat(uint64_t dirfd, uint64_t user_path, uint64_t user_stat)
{
    thread_t *thread = scheduler_current();

    if (thread == NULL || thread->process == NULL) {
        return -EPERM;
    }

    if (user_stat == 0) {
        return -EFAULT;
    }

    char path[SYSCALL_MAX_PATH];

    int result = copy_user_path(user_path, path, sizeof(path));

    if (result != 0) {
        return result;
    }

    if ((int64_t)dirfd != AT_FDCWD && path[0] != '/') {
        return -ENOTSUP;
    }

    char resolved[PROCESS_PATH_MAX];

    os_error_t status = path_resolve(thread->process->cwd, path, resolved, sizeof(resolved));

    if (status == OS_ERR_NAME_TOO_LONG) {
        return -ENAMETOOLONG;
    }

    if (status != OS_OK) {
        return -EINVAL;
    }

    vfs_stat_t vfs_info;

    status = vfs_stat(resolved, &vfs_info);

    if (status == OS_ERR_NOT_FOUND) {
        return -ENOENT;
    }

    if (status == OS_ERR_NOT_DIRECTORY) {
        return -ENOTDIR;
    }

    if (status == OS_ERR_ACCESS_DENIED) {
        return -EACCES;
    }

    if (status != OS_OK) {
        return -EIO;
    }

    struct stat info = {0};

    info.st_size = vfs_info.size;
    info.st_mode = vfs_info.type == VFS_TYPE_DIRECTORY ? S_IFDIR : S_IFREG;
    info.st_attributes = vfs_info.attributes;

    info.st_ctime = (os_stat_time_t) {
        vfs_info.created.year, vfs_info.created.month, vfs_info.created.day,
        vfs_info.created.hour, vfs_info.created.minute, vfs_info.created.second,
        vfs_info.created.valid, vfs_info.created.has_time
    };

    info.st_mtime = (os_stat_time_t) {
        vfs_info.modified.year, vfs_info.modified.month, vfs_info.modified.day,
        vfs_info.modified.hour, vfs_info.modified.minute, vfs_info.modified.second,
        vfs_info.modified.valid, vfs_info.modified.has_time
    };

    info.st_atime = (os_stat_time_t) {
        vfs_info.accessed.year, vfs_info.accessed.month, vfs_info.accessed.day,
        vfs_info.accessed.hour, vfs_info.accessed.minute, vfs_info.accessed.second,
        vfs_info.accessed.valid, vfs_info.accessed.has_time
    };

    return copy_to_user((void *)user_stat, &info, sizeof(info));
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
 * Reads directory entries into a Ring 3 buffer.
 *
 * Validates the entire destination range before advancing
 * the directory cursor.
 */
static int64_t syscall_getdents64(uint64_t fd,
                                  uint64_t user_buffer,
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

    if (thread->process->files[fd].type != PROCESS_FD_DIRECTORY) {
        return -ENOTDIR;
    }

    if (count == 0 || count > INT64_MAX ||
        count - 1 > UINT64_MAX - user_buffer) {
        return -EINVAL;
    }

    // Validate every destination page before consuming entries.
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

    // Fixed-size kernel staging buffer.
    // A record can be larger than this, so we use a buffer
    // sufficient for the largest VFS filename.
    uint8_t buffer[DIRENT64_RECLEN(VFS_MAX_NAME - 1)];

    size_t capacity = (size_t)count;

    if (capacity > sizeof(buffer)) {
        capacity = sizeof(buffer);
    }

    int64_t result = process_getdents64(
        thread->process, (int)fd, buffer, capacity
    );

    if (result <= 0) {
        return result;
    }

    int copy_result = copy_to_user(
        (void *)user_buffer, buffer, (size_t)result
    );

    if (copy_result != 0) {
        return copy_result;
    }

    return result;
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

        case SYS_OPENAT:
            return (uint64_t)syscall_openat(arg1, arg2, arg3);

        case SYS_GETDENTS64:
            return (uint64_t)syscall_getdents64(arg1, arg2, arg3);

        case SYS_FSTATAT:
            return (uint64_t)syscall_fstatat(arg1, arg2, arg3);

        default:
            return (uint64_t)-ENOSYS;
    }
}