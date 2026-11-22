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
#include <stdint.h>
#include <stdbool.h>
#include <vfs.h>
#include <mem.h>
#include <str.h>
#include <process.h>
#include <vm.h>
#include <usermode.h>
#include <process_exec.h>

#include "../exec/loader.h"

/*
 * Reads the first userland executable from FAT32 and verifies
 * that the existing ELF loader accepts its program headers.
 */
bool elf_execution_test_run(void)
{
    klogf("ELF execution test: ENTERING /bin/hello\n");

    char *argv[] = { "hello", NULL };
    bool passed = process_exec("/bin/hello", 1, argv);

    klogf("ELF execution test: %s\n", passed ? "PASS" : "FAIL");

    return passed;
}