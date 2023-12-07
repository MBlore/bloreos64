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
size_t ultoa(uint64_t num, char str[], size_t base)
{
    size_t i = 0;

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

    // Null-terminate the string
    str[i] = '\0';

    // Reverse the string
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

/* Converts a float to a null-terminated string storing the result in the str parameter
   No float support in kernel space.
size_t ftoa(float f, char str[], int precision)
{
    // Seperate the parts
    int intPart = (int)f;
    float fracPart = f - intPart;

    int digits = 0;
    while (intPart) {
        str[digits++] = (char)(intPart % 10 + '0');
        intPart /= 10;
    }

    // Handle no fractional part.
    if (!fracPart) {
        if (digits > 0) {
            str[digits++] = '.';
            str[digits++] = '0';
        }
        str[digits++] = '\0';
        return digits;
    }

    int fracDigits = precision;
    while (fracDigits--) {
        fracPart *= 10.0f;
        int digit = (int)fracPart;
        fracPart -= digit;

        str[digits++] = (char)(digit + '0');
        if (fracPart == 0) {
            break;
        }
    }

    str[digits++] = '.';
    str[digits] = '\0';

    return digits;
}
*/

/* Formats a series of arguments in specific formats and stores the string equivalents in buffer */
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
            } else if (format[i] == 's') {
                char *str = va_arg(args, char*);
                while (*str != '\0') {
                    buffer[bufferIndex++] = *str++;
                }
            } else if (format[i] == 'l' && format[i+1] == 'u') {
                ++i; // Move past the 2nd char format token
                uint64_t value = va_arg(args, uint64_t);
                bufferIndex += ultoa(value, buffer + bufferIndex, 10);
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