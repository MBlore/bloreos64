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
#include <stddef.h>

#include "elf.h"

/*
 * Validates the file bounds, memory size and alignment of an ELF64 loadable segment.
 *
 * This checks the segment's structure only. Virtual address allocation,
 * page permissions and overlap checks are handled by the loader.
 */
static bool elf64_validate_segment(const elf64_program_header_t *segment, size_t file_size)
{
    // A segment cannot require fewer bytes in memory than it contains in the file.
    if (segment->filesz > segment->memsz) {
        return false;
    }

    // The segment's file data must be entirely contained within the ELF file.
    if (segment->offset > file_size) {
        return false;
    }

    if (segment->filesz > file_size - segment->offset) {
        return false;
    }

    // Prevent overflow when calculating the segment's ending virtual address.
    if (segment->memsz > UINT64_MAX - segment->vaddr) {
        return false;
    }

    // ELF alignment must be zero, one or a power of two.
    if (segment->align > 1 && (segment->align & (segment->align - 1)) != 0) {
        return false;
    }

    // File offsets and virtual addresses must agree modulo the alignment.
    if (segment->align > 1 &&
        (segment->vaddr % segment->align) != (segment->offset % segment->align)) {
        return false;
    }

    return true;
}

/*
 * Validates the ELF64 header of a potential x86-64 executable.
 *
 * This performs basic format validation only. Program header bounds,
 * segment layouts and memory permissions will be validated by the loader.
 */
bool elf64_validate(const void *data, size_t size)
{
    if (data == NULL || size < sizeof(elf64_header_t)) {
        return false;
    }

    const elf64_header_t *header = (const elf64_header_t *)data;

    if (header->ident[0] != 0x7F ||
        header->ident[1] != 'E' ||
        header->ident[2] != 'L' ||
        header->ident[3] != 'F') {
        return false;
    }

    if (header->ident[4] != ELF_CLASS_64) {
        return false;
    }

    if (header->ident[5] != ELF_DATA_LSB) {
        return false;
    }

    if (header->ident[6] != ELF_VERSION_CURRENT) {
        return false;
    }

    if (header->type != ELF_TYPE_EXEC) {
        return false;
    }

    if (header->machine != ELF_MACHINE_X86_64) {
        return false;
    }

    if (header->version != ELF_VERSION_CURRENT) {
        return false;
    }

    if (header->ehsize != sizeof(elf64_header_t)) {
        return false;
    }

    if (header->phnum == 0) {
        return false;
    }

    if (header->phentsize != sizeof(elf64_program_header_t)) {
        return false;
    }

    if (header->phoff > size) {
        return false;
    }

    if ((uint64_t)header->phnum > (size - header->phoff) / sizeof(elf64_program_header_t)) {
        return false;
    }

    // Validate PT_LOAD segments.
    const uint8_t *bytes = (const uint8_t *)data;
    bool has_loadable_segment = false;

    for (uint16_t i = 0; i < header->phnum; i++) {
        const elf64_program_header_t *segment =
            (const elf64_program_header_t *)(bytes + header->phoff +
                                            (size_t)i * sizeof(elf64_program_header_t));

        if (segment->type != ELF_PT_LOAD) {
            continue;
        }

        if (!elf64_validate_segment(segment, size)) {
            return false;
        }

        has_loadable_segment = true;
    }

    if (!has_loadable_segment) {
        return false;
    }

    return true;
}