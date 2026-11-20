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
#include <stdint.h>
#include <stdbool.h>

#include "elf.h"
#include "loader.h"

#define ELF_USER_MIN_ADDRESS 0x0000000000001000ULL
#define ELF_USER_MAX_ADDRESS 0x0000800000000000ULL

#define ELF_PT_INTERP 3
#define ELF_PT_DYNAMIC 2

/*
 * Checks whether two ELF segments occupy overlapping virtual address ranges.
 *
 * Zero-sized segments do not occupy any memory.
 */
static bool elf_segments_overlap(const elf_load_segment_t *a, const elf_load_segment_t *b)
{
    if (a->memory_size == 0 || b->memory_size == 0) {
        return false;
    }

    const uint64_t a_end = a->virtual_address + a->memory_size;
    const uint64_t b_end = b->virtual_address + b->memory_size;

    return a->virtual_address < b_end && b->virtual_address < a_end;
}

/*
 * Builds a validated loading plan for a static ELF64 executable.
 *
 * Rejects unsupported dynamic linking, invalid user addresses,
 * overlapping segments and entry points outside executable memory.
 */
bool elf_loader_plan(const void *data, size_t size, elf_load_plan_t *plan)
{
    if (plan == NULL || !elf64_validate(data, size)) {
        return false;
    }

    const elf64_header_t *header = (const elf64_header_t *)data;
    const uint8_t *bytes = (const uint8_t *)data;

    elf_load_plan_t result = {0};
    result.entry_point = header->entry;

    bool entry_is_executable = false;

    for (uint16_t i = 0; i < header->phnum; i++) {
        const elf64_program_header_t *segment =
            (const elf64_program_header_t *)(bytes + header->phoff +
                                             (size_t)i * sizeof(elf64_program_header_t));

        // Dynamic executables and ELF interpreters are not supported yet.
        if (segment->type == ELF_PT_INTERP || segment->type == ELF_PT_DYNAMIC) {
            return false;
        }

        if (segment->type != ELF_PT_LOAD) {
            continue;
        }

        if (result.segment_count >= ELF_LOADER_MAX_SEGMENTS) {
            return false;
        }

        // Reject segments outside the supported user address range.
        if (segment->vaddr < ELF_USER_MIN_ADDRESS ||
            segment->vaddr >= ELF_USER_MAX_ADDRESS) {
            return false;
        }

        if (segment->memsz > ELF_USER_MAX_ADDRESS - segment->vaddr) {
            return false;
        }

        // Reject unsupported segment permission bits.
        if (segment->flags & ~(ELF_PF_R | ELF_PF_W | ELF_PF_X)) {
            return false;
        }

        // We do not currently support segments that are both writable and executable.
        if ((segment->flags & (ELF_PF_W | ELF_PF_X)) == (ELF_PF_W | ELF_PF_X)) {
            return false;
        }

        elf_load_segment_t *destination = &result.segments[result.segment_count];

        destination->virtual_address = segment->vaddr;
        destination->file_offset = segment->offset;
        destination->file_size = segment->filesz;
        destination->memory_size = segment->memsz;
        destination->flags = segment->flags;

        // Check for overlapping virtual memory regions.
        for (uint16_t j = 0; j < result.segment_count; j++) {
            if (elf_segments_overlap(destination, &result.segments[j])) {
                return false;
            }
        }

        // The entry point must fall inside an executable segment.
        if ((segment->flags & ELF_PF_X) &&
            header->entry >= segment->vaddr &&
            header->entry - segment->vaddr < segment->memsz) {
            entry_is_executable = true;
        }

        result.segment_count++;
    }

    if (!entry_is_executable) {
        return false;
    }

    *plan = result;

    return true;
}