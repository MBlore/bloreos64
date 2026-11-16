#include <stdint.h>

void write_serial_str(uint16_t port, char *str)
{
    (void)port;
    (void)str;
}

void tprintf(const char format[], ...)
{
    (void)format;
}