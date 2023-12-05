#ifndef _BLOREOS_STR_H
#define _BLOREOS_STR_H

#include <stddef.h>
#include <stdarg.h>

void reverse(char str[], size_t length);
size_t itoa(int num, char str[], size_t base);

int snprintf(char buffer[], size_t size, const char format[], ...);
int vsnprintf(char buffer[], size_t size, const char format[], va_list args);

#endif