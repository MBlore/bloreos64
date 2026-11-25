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
#include <stdbool.h>
#include <serial.h>
#include <terminal/terminal.h>
#include <limits.h>

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

/*
 * Converts a signed integer to a null-terminated string using
 * the specified base, including the minimum signed integer.
 */
size_t itoa(int num, char str[], size_t base)
{
    if (str == NULL || base < 2 || base > 36) {
        return 0;
    }

    bool negative = num < 0;
    unsigned int magnitude = negative ? (unsigned int)-(num + 1) + 1u : (unsigned int)num;
    size_t i = 0;

    do {
        unsigned int digit = magnitude % base;
        str[i++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        magnitude /= base;
    } while (magnitude != 0);

    if (negative) {
        str[i++] = '-';
    }

    str[i] = '\0';
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

    // Null-terminate the string
    str[i] = '\0';

    // Reverse the string to correct the ordering.
    reverse(str, i);

    return i;
}

/*
 * Converts a signed 64-bit integer to a null-terminated string
 * without overflowing when the value is INT64_MIN.
 */
size_t ltoa(int64_t num, char str[], size_t base)
{
    if (str == NULL || base < 2 || base > 36) {
        return 0;
    }

    bool negative = num < 0;
    uint64_t magnitude = negative ? (uint64_t)-(num + 1) + 1u : (uint64_t)num;
    size_t i = 0;

    do {
        uint64_t digit = magnitude % base;
        str[i++] = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        magnitude /= base;
    } while (magnitude != 0);

    if (negative) {
        str[i++] = '-';
    }

    str[i] = '\0';
    reverse(str, i);

    return i;
}

/*
 * Appends a character to a bounded formatting buffer.
 *
 * Always increments the logical output length, even when the
 * destination buffer is full.
 */
static void format_append_char(char *buffer, size_t size, size_t *length, char ch)
{
    if (buffer != NULL && *length < size && *length < size - 1) {
        buffer[*length] = ch;
    }

    if (*length < SIZE_MAX) {
        (*length)++;
    }
}

/*
 * Appends a null-terminated string to a bounded formatting buffer.
 */
static void format_append_string(char *buffer, size_t size, size_t *length, const char *str)
{
    while (*str != '\0') {
        format_append_char(buffer, size, length, *str++);
    }
}


/*
 * Formats text into a bounded destination buffer.
 *
 * Supports %c, %s, %d, %i, %u, %x, %X, %ld, %lu and %%.
 * Hexadecimal formatting retains BloreOS's existing 16-digit
 * zero-padding behaviour.
 *
 * Returns the number of characters that would have been written,
 * excluding the null terminator. The output is null-terminated
 * whenever the destination size is greater than zero.
 */
int vsnprintf(char buffer[], size_t size, const char format[], va_list args)
{
    if (format == NULL || (size > 0 && buffer == NULL)) {
        return -1;
    }

    size_t length = 0;
    char number[32];

    for (size_t i = 0; format[i] != '\0'; i++) {
        if (format[i] != '%') {
            format_append_char(buffer, size, &length, format[i]);
            continue;
        }

        i++;

        if (format[i] == '\0') {
            format_append_char(buffer, size, &length, '%');
            break;
        }

        switch (format[i]) {
            case '%':
                format_append_char(buffer, size, &length, '%');
                break;

            case 'c': {
                int ch = va_arg(args, int);
                format_append_char(buffer, size, &length, (char)ch);
                break;
            }

            case 's': {
                const char *str = va_arg(args, const char *);
                format_append_string(buffer, size, &length, str != NULL ? str : "(null)");
                break;
            }

            case 'd':
            case 'i': {
                int value = va_arg(args, int);
                itoa(value, number, 10);
                format_append_string(buffer, size, &length, number);
                break;
            }

            case 'u': {
                unsigned int value = va_arg(args, unsigned int);
                __ultoa(value, number, 10);
                format_append_string(buffer, size, &length, number);
                break;
            }

            case 'x': {
                uint64_t value = va_arg(args, uint64_t);
                __ultoa(value, number, 16);
                format_append_string(buffer, size, &length, number);
                break;
            }

            case 'X': {
                uint64_t value = va_arg(args, uint64_t);
                __ultoua(value, number, 16);
                format_append_string(buffer, size, &length, number);
                break;
            }

            case 'l': {
                if (format[i + 1] == 'u') {
                    i++;
                    unsigned long value = va_arg(args, unsigned long);
                    __ultoa(value, number, 10);
                    format_append_string(buffer, size, &length, number);
                } else if (format[i + 1] == 'd') {
                    i++;
                    long value = va_arg(args, long);
                    ltoa(value, number, 10);
                    format_append_string(buffer, size, &length, number);
                } else {
                    format_append_char(buffer, size, &length, '%');
                    format_append_char(buffer, size, &length, 'l');
                }

                break;
            }

            default:
                format_append_char(buffer, size, &length, '%');
                format_append_char(buffer, size, &length, format[i]);
                break;
        }
    }

    if (size > 0) {
        buffer[length < size ? length : size - 1] = '\0';
    }

    // An int cannot represent an output length larger than INT_MAX.
    if (length > INT_MAX) {
        return -1;
    }

    return (int)length;
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
    while (*l && *l == *r) {
        l++;
        r++;
    }

    return (unsigned char)*l - (unsigned char)*r;
}

/*
    Kernel print - currently writes to serial COM1.
*/
void kprintf(const char format[], ...)
{
    char buffer[256];

    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    write_serial_str(PORT_COM1, buffer);
}

/*
    Kernel log - currently writes to serial COM1 and terminal.
*/
void klogf(const char format[], ...)
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

static unsigned char ascii_tolower(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }

    return c;
}

int strcasecmp(const char *l, const char *r)
{
    while (*l && ascii_tolower(*l) == ascii_tolower(*r)) {
        l++;
        r++;
    }

    // Returns 0 if every char passed compare.
    return ascii_tolower(*l) - ascii_tolower(*r);
}

/*
 * Compares at most n characters of two strings.
 *
 * Returns zero if equal, a negative value if the first string is
 * smaller, or a positive value if the first string is greater.
 */
int strncmp(const char *s1, const char *s2, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c1 = (unsigned char)s1[i];
        unsigned char c2 = (unsigned char)s2[i];

        if (c1 != c2) {
            return (int)c1 - (int)c2;
        }

        if (c1 == '\0') {
            return 0;
        }
    }

    return 0;
}

/*
 * Finds the first occurrence of a character in a null-terminated string.
 *
 * Returns a pointer to the matching character, or NULL if not found.
 * The terminating null character can also be searched for.
 */
char *strchr(const char *str, int ch)
{
    unsigned char target = (unsigned char)ch;

    while (*str != '\0') {
        if ((unsigned char)*str == target) {
            return (char *)str;
        }

        str++;
    }

    return target == '\0' ? (char *)str : NULL;
}