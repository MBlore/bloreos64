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
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <serial.h>
#include <terminal.h>

/* Reverses the string in str */
void reverse(char str[], size_t length)
{
    size_t start = 0;
    size_t end = length - 1;

    while (start < end) {
        // Swap characters at start and end
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;

        // Move towards the center
        ++start;
        --end;
    }
}

/* Converts an integer value to a null-terminated string storing the result in the str parameter */
size_t itoa(int num, char str[], size_t base)
{
    size_t i = 0;

    // Handle negative numbers
    int isNegative = 0;
    if (num < 0)
    {
        isNegative = 1;
        num = -num;
    }

    // Handle 0 explicitly
    if (num == 0)
    {
        str[i++] = '0';
    }

    // Process individual digits
    while (num != 0)
    {
        size_t rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    // Add the negative sign if necessary
    if (isNegative)
    {
        str[i++] = '-';
    }

    // Null-terminate the string
    str[i] = '\0';

    // Reverse the string
    reverse(str, i);

    return i;
}

/* Converts a uint64_t (unsigned long) value to a null-terminated string storing the result in the str parameter */
size_t __ultoa(uint64_t num, char str[], size_t base)
{
    size_t i = 0;

    // Process individual digits
    do
    {
        size_t rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    } while (num != 0);

    // Pad out the rest of the string with 0's.
    if (base == 16) {
        while (i < 16) {
            str[i++] = '0';
        }
    }

    // Null-terminate the string
    str[i] = '\0';

    // Reverse the string to correct the ordering.
    reverse(str, i);

    return i;
}

/*
    Converts a uint64_t (unsigned long) value to a null-terminated string storing the result in the str parameter
    When using base > 10, the chars are upper-case.
*/
size_t __ultoua(uint64_t num, char str[], size_t base)
{
    size_t i = 0;

    // Process individual digits
    do
    {
        size_t rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'A' : rem + '0';
        num = num / base;
    } while (num != 0);

    // Pad out the rest of the string with 0's.
    if (base == 16) {
        while (i < 16) {
            str[i++] = '0';
        }
    }

    // Null-terminate the string
    str[i] = '\0';

    // Reverse the string to correct the ordering.
    reverse(str, i);

    return i;
}

/* Converts an int64_t value to a null-terminated string storing the result in the str parameter */
size_t ltoa(int64_t num, char str[], size_t base)
{
    size_t i = 0;

    // Handle negative numbers
    int isNegative = 0;
    if (num < 0)
    {
        isNegative = 1;
        num = -num;
    }

    // Handle 0 explicitly
    if (num == 0)
    {
        str[i++] = '0';
    }

    // Process individual digits
    while (num != 0)
    {
        size_t rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    // Add the negative sign if necessary
    if (isNegative)
    {
        str[i++] = '-';
    }

    // Null-terminate the string
    str[i] = '\0';

    // Reverse the string
    reverse(str, i);

    return i;
}

/*
    Formats a series of arguments in specific formats and stores the string equivalents in buffer

    %c - single char
    %d - int
    %x - uint64 as hexadecimal
    %X - uint64 as hexadecimal upper-case
    %s - string
    %lu - uint64
    %ld - int64
*/
int vsnprintf(char buffer[], size_t size, const char format[], va_list args)
{
    size_t bufferIndex = 0;

    // Iterate through the format string
    for (size_t i = 0; format[i] != '\0'; ++i) {
        if (format[i] == '%') {
            ++i; // Move past '%'

            // Handle format specifiers
            if (format[i] == 'd') {
                int value = va_arg(args, int);
                bufferIndex += itoa(value, buffer + bufferIndex, 10);
            } else if (format[i] == 'x') {
                uint64_t value = va_arg(args, uint64_t);
                bufferIndex += __ultoa(value, buffer + bufferIndex, 16);
            } else if (format[i] == 'X') {
                uint64_t value = va_arg(args, uint64_t);
                bufferIndex += __ultoua(value, buffer + bufferIndex, 16);
            } else if (format[i] == 's') {
                char *str = va_arg(args, char*);
                while (*str != '\0') {
                    buffer[bufferIndex++] = *str++;
                }
            } else if (format[i] == 'c') {
                int ch = va_arg(args, int);
                buffer[bufferIndex++] = (char)ch;
            } else if (format[i] == 'l' && format[i+1] == 'u') {
                ++i; // Move past the 2nd char format token
                uint64_t value = va_arg(args, uint64_t);
                bufferIndex += __ultoa(value, buffer + bufferIndex, 10);
            } else if (format[i] == 'l' && format[i+1] == 'd') {
                ++i; // Move past the 2nd char format token
                int64_t value = va_arg(args, int64_t);
                bufferIndex += ltoa(value, buffer + bufferIndex, 10);
            } 
        }
        else {
            // Check if there is space in the buffer
            if (bufferIndex < size - 1) {
                buffer[bufferIndex++] = format[i];
            }
            else {
                // Truncate if the buffer is full
                break;
            }
        }
    }

    // Null-terminate the resulting string
    if (bufferIndex < size) {
        buffer[bufferIndex] = '\0';
    }
    else {
        // Ensure null termination in case of truncation
        buffer[size - 1] = '\0';
    }

    return bufferIndex;
}

int snprintf(char buffer[], size_t size, const char format[], ...)
{
    va_list args;
    va_start(args, format);
    int r = vsnprintf(buffer, size, format, args);
    va_end(args);

    return r;
}

size_t strlen(const char *str)
{
    size_t len = 0;

    for (int i = 0; i < 65535; i++) {
        if (str[i] == '\0') {
            return len;
        }
        len++;
    }

    return -1;
}

int strcmp(const char *l, const char *r)
{
    while (*l && *r) {
        if (*l != *r) {
            return -1;
        }

        l++;
        r++;
    }

    return 0;
}

/*
    Kernel log - currently writes to serial COM1 and the terminal.
*/
void kprintf(const char format[], ...)
{
    char buffer[256];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    write_serial_str(PORT_COM1, buffer);

    tprintf(buffer);
}

/*
 * Prints the specified uint8_t to a char buffer in binary form.
 * buff must be at least 9 bytes.
 * Bits printed right-to-left, 765433210.
*/
void sprint_binary8(char buff[], uint8_t ch)
{
    for (int i = 0; i < 8; i++) {
        buff[7-i] = ch & (1 << i) ? '1' : '0';
    }

    buff[8] = '\0';
}

/*
 * Prints the specified uint32_t to a char buffer in binary form.
 * buff must be at least 18 bytes.
 * Bits printed right-to-left.
*/
void sprint_binary16(char buff[], uint16_t ch)
{
    buff[17] = '\0';
    int bitIndex, charIndex = 0;

    for (bitIndex = 0; bitIndex < 16; bitIndex++) {
        buff[charIndex++] = ((ch << bitIndex) & 0x8000) ? '1' : '0';

        if ((bitIndex + 1) % 8 == 0 && bitIndex != 15) {
            buff[charIndex++] = ' ';
        }
    }
}

/*
 * Prints the specified uint32_t to a char buffer in binary form.
 * buff must be at least 36 bytes.
 * Bits printed right-to-left.
*/
void sprint_binary32(char buff[], uint32_t ch)
{
    buff[35] = '\0';
    int bitIndex, charIndex = 0;

    for (bitIndex = 0; bitIndex < 32; bitIndex++) {
        buff[charIndex++] = ((ch << bitIndex) & 0x80000000) ? '1' : '0';

        if ((bitIndex + 1) % 8 == 0 && bitIndex != 31) {
            buff[charIndex++] = ' ';
        }
    }
}

/*
 * Prints the specified uint32_t to a char buffer in binary form.
 * buff must be at least 72 bytes.
 * Bits printed right-to-left.
*/
void sprint_binary64(char buff[], uint64_t ch)
{
    buff[71] = '\0';
    int bitIndex, charIndex = 0;

    for (bitIndex = 0; bitIndex < 64; bitIndex++) {
        buff[charIndex++] = ((ch << bitIndex) & 0x8000000000000000) ? '1' : '0';

        if ((bitIndex + 1) % 8 == 0 && bitIndex != 63) {
            buff[charIndex++] = ' ';
        }
    }
}