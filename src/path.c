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

#include <path.h>
#include <str.h>
#include <string.h>

/*
 * Removes the last component of an absolute path.
 *
 * Parameters:
 *   path   - Mutable absolute path.
 *   length - Current length, updated on return.
 */
static void path_parent(char *path, size_t *length)
{
    if (*length <= 1) {
        return;
    }

    while (*length > 1 && path[*length - 1] != '/') {
        (*length)--;
    }

    if (*length > 1) {
        (*length)--;
    }

    path[*length] = '\0';
}

/*
 * Resolves a pathname against the supplied working directory.
 *
 * Handles ".", "..", repeated separators and trailing slashes.
 * Does not check whether the resolved path exists.
 */
os_error_t path_resolve(const char *cwd,
                        const char *input,
                        char *output,
                        size_t capacity)
{
    if (cwd == NULL || input == NULL ||
        output == NULL || capacity < 2 ||
        cwd[0] != '/' || input[0] == '\0') {
        return OS_ERR_INVALID_ARGUMENT;
    }

    size_t length = 1;
    output[0] = '/';
    output[1] = '\0';

    if (input[0] != '/') {
        size_t cwd_length = strlen(cwd);

        if (cwd_length >= capacity) {
            return OS_ERR_NAME_TOO_LONG;
        }

        memcpy(output, cwd, cwd_length + 1);
        length = cwd_length;

        // Accept a trailing slash in cwd without duplicating it.
        while (length > 1 && output[length - 1] == '/') {
            output[--length] = '\0';
        }
    }

    const char *cursor = input;

    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }

        if (*cursor == '\0') {
            break;
        }

        const char *component = cursor;

        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }

        size_t component_length = (size_t)(cursor - component);

        if (component_length == 1 && component[0] == '.') {
            continue;
        }

        if (component_length == 2 &&
            component[0] == '.' && component[1] == '.') {
            path_parent(output, &length);
            continue;
        }

        size_t separator = length > 1 ? 1 : 0;

        if (component_length >= capacity - length - separator) {
            return OS_ERR_NAME_TOO_LONG;
        }

        if (separator != 0) {
            output[length++] = '/';
        }

        memcpy(output + length, component, component_length);
        length += component_length;
        output[length] = '\0';
    }

    return OS_OK;
}
