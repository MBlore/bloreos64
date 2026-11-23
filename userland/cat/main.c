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
#include <abi/syscall.h>
#include <abi/unistd.h>

/*
 * Invokes a BloreOS syscall through interrupt 0x80.
 *
 * Parameters:
 *   number - Syscall number.
 *   arg1   - First argument.
 *   arg2   - Second argument.
 *   arg3   - Third argument.
 *
 * Returns:
 *   Syscall result, or negative errno on failure.
 */
static inline int64_t syscall3(uint64_t number,
                               uint64_t arg1,
                               uint64_t arg2,
                               uint64_t arg3)
{
    uint64_t result;

    __asm__ volatile (
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
        : "memory", "cc"
    );

    return (int64_t)result;
}

/*
 * Writes all bytes in a buffer to a file descriptor.
 *
 * Parameters:
 *   fd     - Destination file descriptor.
 *   buffer - Source buffer.
 *   length - Number of bytes to write.
 *
 * Returns:
 *   Zero on success, otherwise -1.
 */
static int write_all(uint64_t fd, const char *buffer, size_t length)
{
    size_t total = 0;

    while (total < length) {
        int64_t written = syscall3(
            SYS_WRITE,
            fd,
            (uint64_t)(buffer + total),
            length - total
        );

        if (written <= 0 || (uint64_t)written > length - total) {
            return -1;
        }

        total += (size_t)written;
    }

    return 0;
}

/*
 * Writes a NUL-terminated string to standard error.
 *
 * Parameters:
 *   message - Error message to print.
 */
static void print_error(const char *message)
{
    size_t length = 0;

    while (message[length] != '\0') {
        length++;
    }

    (void)write_all(STDERR_FILENO, message, length);
}

/*
 * Prints a file's contents to standard output.
 *
 * Parameters:
 *   argc - Argument count.
 *   argv - Argument strings.
 *
 * Returns:
 *   Zero on success, otherwise one.
 */
int main(int argc, char **argv)
{
    if (argc != 2) {
        print_error("Usage: cat <file>\n");
        return 1;
    }

    int64_t fd = syscall3(
        SYS_OPEN,
        (uint64_t)argv[1],
        0,
        0
    );

    if (fd < 0) {
        print_error("cat: Cannot open file\n");
        return 1;
    }

    char buffer[256];
    int status = 0;

    for (;;) {
        int64_t count = syscall3(
            SYS_READ,
            (uint64_t)fd,
            (uint64_t)buffer,
            sizeof(buffer)
        );

        if (count < 0) {
            print_error("cat: Read failed\n");
            status = 1;
            break;
        }

        if (count == 0) {
            break;
        }

        if (write_all(STDOUT_FILENO, buffer, (size_t)count) != 0) {
            print_error("cat: Write failed\n");
            status = 1;
            break;
        }
    }

    if (syscall3(SYS_CLOSE, (uint64_t)fd, 0, 0) < 0) {
        print_error("cat: Close failed\n");
        status = 1;
    }

    return status;
}