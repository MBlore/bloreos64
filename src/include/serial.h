#ifndef _BLOREOS_SERIAL_H
#define _BLOREOS_SERIAL_H

#include <stdint.h>

#define PORT_COM1 (uint16_t)0x3F8

int init_serial(uint16_t port);
void write_serial(uint16_t port, char a);
void write_serial_str(uint16_t port, char *str);
void write_serial_strf(uint16_t port, const char format[], ...);

#endif