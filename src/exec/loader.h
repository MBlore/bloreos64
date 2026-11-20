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
#ifndef _BLOREOS_LOADER_H
#define _BLOREOS_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <process.h>

#define ELF_LOADER_MAX_SEGMENTS 16

typedef struct elf_load_segment {
    uint64_t virtual_address;   // Virtual address where the segment must be loaded.
    uint64_t file_offset;       // Byte offset of the segment within the ELF file.
    uint64_t file_size;         // Number of bytes to copy from the ELF file.
    uint64_t memory_size;       // Total number of bytes occupied in memory.
    uint32_t flags;             // Segment permissions (read, write, execute).
} elf_load_segment_t;

typedef struct elf_load_plan {
    uint64_t entry_point;       // Virtual address where execution begins.
    uint16_t segment_count;     // Number of loadable segments.
    elf_load_segment_t segments[ELF_LOADER_MAX_SEGMENTS];
} elf_load_plan_t;

bool elf_loader_plan(const void *data, size_t size, elf_load_plan_t *plan);
bool elf_loader_allocate_segments(process_t *process, const elf_load_plan_t *plan);
bool elf_loader_copy_segments(process_t *process, const void *data, size_t size,
                              const elf_load_plan_t *plan);
bool elf_loader_load(process_t *process, const void *data, size_t size, uint64_t *entry_point);
bool elf_loader_allocate_stack(process_t *process);

#endif