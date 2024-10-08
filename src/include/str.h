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
#ifndef _BLOREOS_STR_H
#define _BLOREOS_STR_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/*
    Formatting symbols.

    %c - single char
    %d - int
    %x - uint64 as hexadecimal
    %X - uint64 as hexadecimal upper-case
    %s - string
    %lu - uint64
    %ld - int64
*/

void reverse(char str[], size_t length);

size_t itoa(int num, char str[], size_t base);
size_t ltoa(int64_t num, char str[], size_t base);
size_t ultoa(uint64_t num, char str[], size_t base);

size_t strlen(const char *str);
int strcmp(const char *l, const char *r);

int snprintf(char buffer[], size_t size, const char format[], ...);
int vsnprintf(char buffer[], size_t size, const char format[], va_list args);
void kprintf(const char format[], ...);

void sprint_binary8(char s[], uint8_t ch);
void sprint_binary16(char s[], uint16_t ch);
void sprint_binary32(char buff[], uint32_t ch);
void sprint_binary64(char buff[], uint64_t ch);

#endif