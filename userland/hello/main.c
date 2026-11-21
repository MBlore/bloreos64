#include <stdint.h>
#include <stddef.h>
#include <abi/syscall.h>
#include <abi/unistd.h>

/*
 * Invokes a BloreOS syscall using the x86-64 interrupt ABI.
 *
 * RAX contains the syscall number and receives the return value.
 * RDI, RSI and RDX contain the first three arguments.
 */
static inline int64_t syscall3(uint64_t number, uint64_t arg1, uint64_t arg2, uint64_t arg3)
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
 * Entry point for the first BloreOS user-space executable.
 *
 * Writes a message to stdout, verifies the byte count and exits.
 */
__attribute__((noreturn))
void _start(void)
{
    static const char message[] = "Hello from BloreOS user space!\n";
    static const char error[] = "SYS_WRITE failed!\n";

    int64_t written = syscall3(SYS_WRITE, STDOUT_FILENO, (uint64_t)message, sizeof(message) - 1);

    if (written != (int64_t)(sizeof(message) - 1)) {
        syscall3(SYS_WRITE, STDERR_FILENO, (uint64_t)error, sizeof(error) - 1);
        syscall3(SYS_EXIT, 1, 0, 0);
    }

    syscall3(SYS_EXIT, 0, 0, 0);

    for (;;) {
        __asm__ volatile ("pause");
    }
}