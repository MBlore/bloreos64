
#include <stdint.h>
#include <stddef.h>
#include <abi/syscall.h>
#include <abi/errno.h>
#include <abi/fcntl.h>
#include <abi/dirent.h>

static int64_t syscall3(uint64_t number, uint64_t a,
                        uint64_t b, uint64_t c)
{
    register uint64_t rax __asm__("rax") = number;
    register uint64_t rdi __asm__("rdi") = a;
    register uint64_t rsi __asm__("rsi") = b;
    register uint64_t rdx __asm__("rdx") = c;

    __asm__ volatile (
        "int $0x80"
        : "+a"(rax)
        : "D"(rdi), "S"(rsi), "d"(rdx)
        : "memory", "cc"
    );

    return (int64_t)rax;
}

static void print(const char *s)
{
    size_t length = 0;
    while (s[length] != '\0') length++;

    syscall3(SYS_WRITE, 1, (uint64_t)s, length);
}

static void check(const char *name, int passed)
{
    print(passed ? "[PASS] " : "[FAIL] ");
    print(name);
    print("\n");
}

int main(void)
{
    char buffer[1024];

    int64_t fd = syscall3(
        SYS_OPENAT,
        (uint64_t)(int64_t)AT_FDCWD,
        (uint64_t)"/bin",
        O_RDONLY | O_DIRECTORY
    );

    check("open /bin", fd >= 3);

    if (fd < 0) return 1;

    // Test invalid descriptor.
    check("invalid fd",
          syscall3(SYS_GETDENTS64, 123,
                   (uint64_t)buffer, sizeof(buffer)) == -EBADF);

    // Test zero-length buffer.
    check("zero buffer",
          syscall3(SYS_GETDENTS64, fd,
                   (uint64_t)buffer, 0) == -EINVAL);

    // A one-byte buffer cannot hold a directory record.
    check("tiny buffer",
          syscall3(SYS_GETDENTS64, fd,
                   (uint64_t)buffer, 1) == -EINVAL);

    print("\nDirectory entries:\n");

    int found = 0;
    int failed = 0;
    int64_t result;

    // Small buffer forces repeated calls for larger directories.
    do {
        result = syscall3(
            SYS_GETDENTS64,
            fd,
            (uint64_t)buffer,
            128
        );

        if (result < 0) {
            failed = 1;
            break;
        }

        size_t offset = 0;

        while (offset < (size_t)result) {
            struct linux_dirent64 *entry =
                (struct linux_dirent64 *)(buffer + offset);

            size_t remaining = (size_t)result - offset;

            if (remaining < offsetof(struct linux_dirent64, d_name) + 1 ||
                entry->d_reclen > remaining ||
                entry->d_reclen <
                    offsetof(struct linux_dirent64, d_name) + 1 ||
                (entry->d_reclen & 7) != 0) {
                failed = 1;
                break;
            }

            print("  ");
            print(entry->d_name);
            print(entry->d_type == DT_DIR ? " [DIR]\n" : "\n");

            found++;
            offset += entry->d_reclen;
        }

        if (failed) break;

    } while (result > 0);

    check("directory enumeration", !failed && found > 0);
    check("reached EOF", !failed && result == 0);

    // EOF should remain stable.
    check("repeated EOF",
          syscall3(SYS_GETDENTS64, fd,
                   (uint64_t)buffer, sizeof(buffer)) == 0);

    check("close directory",
          syscall3(SYS_CLOSE, fd, 0, 0) == 0);

    return failed || found == 0 ? 1 : 0;
}
