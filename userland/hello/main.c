#include <stdint.h>
#include <stddef.h>
#include <abi/syscall.h>
#include <abi/unistd.h>
#include <stdbool.h>

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
 * Writes a NUL-terminated string to standard output.
 *
 * Parameters:
 *   text - String to print.
 */
static bool print(const char *text)
{
    size_t length = 0;

    while (text[length] != '\0') {
        length++;
    }

    int64_t written = syscall3(
        SYS_WRITE, STDOUT_FILENO,
        (uint64_t)text, length
    );

    return written == (int64_t)length;
}

/*
 * Prints an unsigned integer in decimal.
 *
 * Parameters:
 *   value - Number to print.
 */
static void print_number(uint64_t value)
{
    char buffer[21];
    size_t position = sizeof(buffer);

    buffer[--position] = '\0';

    do {
        buffer[--position] = '0' + (value % 10);
        value /= 10;
    } while (value != 0);

    print(&buffer[position]);
}

/*
 * Entry point for the BloreOS hello application.
 *
 * Prints the arguments supplied by the shell.
 *
 * Parameters:
 *   argc - Number of command-line arguments.
 *   argv - Array of argument strings.
 *
 * Returns:
 *   Zero on success, otherwise one.
 */
/*
 * Tests argument count and pointer validity independently.
 */
int main(int argc, char **argv)
{
    print("Hello from Ring 3!\n");

    for (int i = 0; i < argc; i++) {
        print("argv[");
        print_number((uint64_t)i);
        print("] = ");
        print(argv[i]);
        print("\n");
    }

    return 0;
}