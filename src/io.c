#include <stdint.h>
#include "io.h"

/* Sends a 8-bit value to a I/O location */
inline void outb(uint16_t port, uint8_t val)
{
    asm volatile("outb %0, %1" : : "a"(val), "nd"(port) : "memory");
}

/* Sends a 16-bit value to a I/O location */
inline void outw(uint16_t port, uint16_t val)
{
    asm volatile("outw %0, %1" : : "a"(val), "nd"(port) : "memory");
}

/* Sends a 32-bit value to a I/O location */
inline void outl(uint16_t port, uint32_t val)
{
    asm volatile("outl %0, %1" : : "a"(val), "nd"(port) : "memory");
}

/* Receives a 8-bit value form an I/O location */
inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    asm volatile("inb %1, %0"
                 : "=a"(ret)
                 : "Nd"(port)
                 : "memory");
    return ret;
}

/* Receives a 16-bit value form an I/O location */
inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    asm volatile("inw %1, %0"
                 : "=a"(ret)
                 : "Nd"(port)
                 : "memory");
    return ret;
}

/* Receives a 32-bit value form an I/O location */
inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    asm volatile("inl %1, %0"
                 : "=a"(ret)
                 : "Nd"(port)
                 : "memory");
    return ret;
}