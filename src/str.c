#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

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