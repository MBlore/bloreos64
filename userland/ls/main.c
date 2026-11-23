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
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stddef.h>
#include <stdint.h>

#define LS_PATH_MAX 1024

/*
 * Appends a string to a bounded destination buffer.
 */
static int append_string(char *buffer, size_t capacity, size_t *length, const char *string)
{
    while (*string != '\0') {
        if (*length >= capacity - 1) {
            return -1;
        }

        buffer[(*length)++] = *string++;
    }

    buffer[*length] = '\0';
    return 0;
}

/*
 * Appends an unsigned decimal number to a bounded buffer.
 */
static int append_number(char *buffer, size_t capacity, size_t *length, uint64_t value)
{
    char digits[21];
    size_t count = 0;

    do {
        digits[count++] = '0' + (value % 10);
        value /= 10;
    } while (value != 0);

    while (count != 0) {
        if (*length >= capacity - 1) {
            return -1;
        }

        buffer[(*length)++] = digits[--count];
    }

    buffer[*length] = '\0';
    return 0;
}

/*
 * Appends a two-digit date or time component.
 */
static int append_two_digits(char *buffer, size_t capacity, size_t *length, uint8_t value)
{
    if (*length > capacity - 3) {
        return -1;
    }

    buffer[(*length)++] = '0' + value / 10;
    buffer[(*length)++] = '0' + value % 10;
    buffer[*length] = '\0';

    return 0;
}

/*
 * Prints one entry with its modification time, type, size and name.
 */
static int print_long_entry(const char *directory, const char *name)
{
    char path[LS_PATH_MAX];
    size_t length = 0;

    path[0] = '\0';

    if (append_string(path, sizeof(path), &length, directory) != 0) {
        return -1;
    }

    if (length != 0 && path[length - 1] != '/') {
        if (append_string(path, sizeof(path), &length, "/") != 0) {
            return -1;
        }
    }

    if (append_string(path, sizeof(path), &length, name) != 0) {
        return -1;
    }

    struct stat info;

    if (stat(path, &info) != 0) {
        return -1;
    }

    char line[LS_PATH_MAX];
    length = 0;
    line[0] = '\0';

    if (info.st_mtime.valid && info.st_mtime.has_time) {
        if (append_number(line, sizeof(line), &length, info.st_mtime.year) != 0 ||
            append_string(line, sizeof(line), &length, "-") != 0 ||
            append_two_digits(line, sizeof(line), &length, info.st_mtime.month) != 0 ||
            append_string(line, sizeof(line), &length, "-") != 0 ||
            append_two_digits(line, sizeof(line), &length, info.st_mtime.day) != 0 ||
            append_string(line, sizeof(line), &length, " ") != 0 ||
            append_two_digits(line, sizeof(line), &length, info.st_mtime.hour) != 0 ||
            append_string(line, sizeof(line), &length, ":") != 0 ||
            append_two_digits(line, sizeof(line), &length, info.st_mtime.minute) != 0) {
            return -1;
        }
    } else {
        if (append_string(line, sizeof(line), &length, "---- -- -- --:--") != 0) {
            return -1;
        }
    }

    if (S_ISDIR(info.st_mode)) {
        if (append_string(line, sizeof(line), &length, "  <DIR>  ") != 0) {
            return -1;
        }
    } else {
        if (append_string(line, sizeof(line), &length, "         ") != 0 ||
            append_string(line, sizeof(line), &length, name) != 0 ||
            append_string(line, sizeof(line), &length, " (") != 0 ||
            append_number(line, sizeof(line), &length, info.st_size) != 0 ||
            append_string(line, sizeof(line), &length, " bytes)") != 0) {
            return -1;
        }

        return puts(line) < 0 ? -1 : 0;
    }

    if (append_string(line, sizeof(line), &length, name) != 0) {
        return -1;
    }

    return puts(line) < 0 ? -1 : 0;
}

/*
 * Lists directory entries in normal or long format.
 */
int main(int argc, char **argv)
{
    int long_format = 0;
    const char *path = ".";

    if (argc >= 2 && argv[1][0] == '-' && argv[1][1] == 'l' && argv[1][2] == '\0') {
        long_format = 1;

        if (argc == 3) {
            path = argv[2];
        } else if (argc > 3) {
            puts("Usage: ls [-l] [path]");
            return 1;
        }
    } else if (argc == 2) {
        path = argv[1];
    } else if (argc > 2) {
        puts("Usage: ls [-l] [path]");
        return 1;
    }

    DIR *dir = opendir(path);

    if (dir == NULL) {
        puts("ls: Cannot open directory");
        return 1;
    }

    struct dirent *entry;
    int result = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (long_format) {
            if (print_long_entry(path, entry->d_name) != 0) {
                puts("ls: Cannot read file metadata");
                result = 1;
                break;
            }
        } else if (puts(entry->d_name) < 0) {
            result = 1;
            break;
        }
    }

    if (closedir(dir) != 0) {
        result = 1;
    }

    return result;
}