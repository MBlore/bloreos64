
#include <stdint.h>
#include <stddef.h>
#include <abi/syscall.h>
#include <abi/errno.h>
#include <abi/fcntl.h>
#include <abi/dirent.h>
#include <dirent.h>

static void print(const char *s);

static int test_dirent(void)
{
    DIR *dir = opendir("/bin");

    if (dir == NULL) {
        print("[FAIL] opendir /bin\n");
        return 1;
    }

    print("[PASS] opendir /bin\n");
    print("\nDirectory entries:\n");

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
        print("  ");
        print(entry->d_name);

        if (entry->d_type == DT_DIR) {
            print(" [DIR]");
        }

        print("\n");
        count++;
    }

    if (count > 0) {
        print("[PASS] readdir\n");
    } else {
        print("[FAIL] readdir\n");
    }

    if (closedir(dir) == 0) {
        print("[PASS] closedir\n");
    } else {
        print("[FAIL] closedir\n");
        return 1;
    }

    return count > 0 ? 0 : 1;
}

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

int main(void)
{
    return test_dirent();
}
