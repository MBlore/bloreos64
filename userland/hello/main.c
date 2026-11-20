#include <stdint.h>

/*
 * Invokes a BloreOS system call through interrupt 0x80.
 *
 * System call 1 prints the kernel's existing diagnostic message.
 * System call 2 terminates the program.
 */
static inline void syscall(uint64_t number)
{
    __asm__ volatile (
        "int $0x80"
        :
        : "a"(number)
        : "memory"
    );
}

/*
 * Entry point for the first BloreOS user-space executable.
 *
 * No C runtime or standard library is required.
 */
__attribute__((noreturn))
void _start(void)
{
    syscall(1);
    syscall(2);

    for (;;) {
        __asm__ volatile ("pause");
    }
}