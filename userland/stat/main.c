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
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Writes a string to standard output without adding a newline.
 */
static int print_string(const char *string)
{
    size_t length = 0;

    while (string[length] != '\0') {
        length++;
    }

    return write(1, string, length) == (ssize_t)length ? 0 : -1;
}

/*
 * Prints an unsigned integer in decimal notation.
 */
static void print_number(uint64_t value)
{
    char buffer[21];
    size_t length = 0;

    do {
        buffer[length++] = '0' + value % 10;
        value /= 10;
    } while (value != 0);

    while (length != 0) {
        char digit = buffer[--length];
        (void)write(1, &digit, 1);
    }
}

/*
 * Prints an unsigned integer in hexadecimal notation.
 */
static void print_hex(uint32_t value)
{
    const char *digits = "0123456789ABCDEF";
    char buffer[8];
    size_t length = 0;

    do {
        buffer[length++] = digits[value & 0xF];
        value >>= 4;
    } while (value != 0);

    while (length != 0) {
        char digit = buffer[--length];
        (void)write(1, &digit, 1);
    }
}

/*
 * Prints a two-digit calendar component.
 */
static void print_two_digits(uint8_t value)
{
    char digits[2] = {
        '0' + value / 10,
        '0' + value % 10
    };

    (void)write(1, digits, sizeof(digits));
}

/*
 * Prints a filesystem timestamp, including time when available.
 */
static void print_datetime(const os_stat_time_t *time)
{
    if (!time->valid) {
        puts("Unknown");
        return;
    }

    print_number(time->year);
    print_string("-");
    print_two_digits(time->month);
    print_string("-");
    print_two_digits(time->day);

    if (time->has_time) {
        print_string(" ");
        print_two_digits(time->hour);
        print_string(":");
        print_two_digits(time->minute);
        print_string(":");
        print_two_digits(time->second);
    }

    print_string("\n");
}

/*
 * Displays metadata for a filesystem object using the userland libc.
 */
int main(int argc, char **argv)
{
    if (argc != 2) {
        puts("Usage: stat <path>");
        return 1;
    }

    struct stat info;

    if (stat(argv[1], &info) != 0) {
        puts("stat: Cannot read file metadata");
        return 1;
    }

    print_string("  File:       ");
    puts(argv[1]);

    print_string("  Type:       ");
    puts(S_ISDIR(info.st_mode) ? "Directory" : "Regular file");

    print_string("  Size:       ");
    print_number(info.st_size);
    puts(" bytes");

    print_string("  Created:    ");
    print_datetime(&info.st_ctime);

    print_string("  Modified:   ");
    print_datetime(&info.st_mtime);

    print_string("  Accessed:   ");
    print_datetime(&info.st_atime);

    print_string("  Attributes: 0x");
    print_hex(info.st_attributes);
    print_string("\n");

    return 0;
}