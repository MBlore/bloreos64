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

#ifndef _BLOREOS_PATH_H
#define _BLOREOS_PATH_H

#include <stddef.h>
#include <error.h>

/*
 * Resolves an absolute or relative pathname.
 *
 * Parameters:
 *   cwd      - Absolute current working directory.
 *   input    - Path to resolve.
 *   output   - Buffer receiving the absolute path.
 *   capacity - Size of output buffer.
 *
 * Returns:
 *   OS_OK on success, otherwise an OS error.
 */
os_error_t path_resolve(const char *cwd,
                        const char *input,
                        char *output,
                        size_t capacity);

#endif
