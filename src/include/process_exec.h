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
#ifndef _BLOREOS_PROCESS_EXEC_H
#define _BLOREOS_PROCESS_EXEC_H

#include <stdbool.h>

/*
 * Loads and executes an ELF64 program as a scheduled Ring 3 process.
 *
 * Parameters:
 *   path - Absolute filesystem path to the executable.
 *   argc - Number of command-line arguments.
 *   argv - Array of argument strings, including the program name.
 *
 * Returns:
 *   True if execution completes successfully, otherwise false.
 */
bool process_exec(const char *path, int argc, char *const argv[]);

#endif