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
#ifndef _BLOREOS_ELF_H
#define _BLOREOS_ELF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ELF_MAGIC 0x464C457F

#define ELF_CLASS_64 2
#define ELF_DATA_LSB 1
#define ELF_VERSION_CURRENT 1

#define ELF_TYPE_EXEC 2
#define ELF_MACHINE_X86_64 62

typedef struct __attribute__((packed)) elf64_header {
    uint8_t ident[16];      // ELF magic bytes, architecture class, byte order and identification metadata.
    uint16_t type;          // Object file type (executable, relocatable, shared object, etc.).
    uint16_t machine;       // Target CPU architecture (62 = x86-64).
    uint32_t version;       // ELF format version (normally 1).
    uint64_t entry;         // Virtual address where execution begins.
    uint64_t phoff;         // File offset of the program header table.
    uint64_t shoff;         // File offset of the section header table.
    uint32_t flags;         // Architecture-specific processor flags.
    uint16_t ehsize;        // Size of this ELF header in bytes (64 for ELF64).
    uint16_t phentsize;     // Size of each program header table entry in bytes.
    uint16_t phnum;         // Number of entries in the program header table.
    uint16_t shentsize;     // Size of each section header table entry in bytes.
    uint16_t shnum;         // Number of entries in the section header table.
    uint16_t shstrndx;      // Index of the section containing section-name strings.
} elf64_header_t;

#define ELF_PT_NULL 0
#define ELF_PT_LOAD 1

#define ELF_PF_X 0x1
#define ELF_PF_W 0x2
#define ELF_PF_R 0x4

typedef struct __attribute__((packed)) elf64_program_header {
    uint32_t type;          // Segment type (PT_LOAD, PT_NULL, PT_DYNAMIC, etc.).
    uint32_t flags;         // Segment permissions (PF_R = read, PF_W = write, PF_X = execute).
    uint64_t offset;        // Byte offset of the segment's data within the ELF file.
    uint64_t vaddr;         // Virtual memory address where the segment should be loaded.
    uint64_t paddr;         // Physical address hint, generally ignored by the OS loader.
    uint64_t filesz;        // Number of bytes occupied by the segment in the ELF file.
    uint64_t memsz;         // Number of bytes required by the segment in memory.
    uint64_t align;         // Required alignment of the segment in memory and file.
} elf64_program_header_t;

bool elf64_validate(const void *data, size_t size);

#endif