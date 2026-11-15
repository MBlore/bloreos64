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
#ifndef _BLOREOS_MEM_H
#define _BLOREOS_MEM_H

#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#define PAGE_SIZE 4096

extern volatile struct limine_memmap_response *memmap;

extern uint64_t total_memory_bytes;
extern uint64_t max_pages_available;
extern uint64_t num_pages_available;
extern uint64_t vmm_higher_half_offset;

// Initialise the physical memory manager using the bootloader's
// memory map. Must only be called once during kernel startup.
void kmem_init(void);

// Reserve zero-initialised physical memory during early boot.
// Memory is rounded up to a page boundary and cannot be freed.
// Halts the kernel on failure.
// Must not be called after PMM initialisation.
void* memmap_alloc(size_t length);

// Allocate contiguous physical memory, rounded up to a page
// boundary. Returns a higher-half virtual address or NULL on
// failure. Allocated memory is not guaranteed to be zeroed.
void* kalloc(size_t numBytes);

// Allocate a specified number of contiguous physical pages.
// Returns a higher-half virtual address or NULL on failure.
void* kpalloc(size_t numPages);

// Release an allocation previously made by kalloc or kpalloc.
// Must receive the original allocation address.
// NULL and unrecognised allocation addresses are ignored.
void kfree(void *ptr);

void memdumps(void *location, uint64_t len_bytes);
void memdumpx32(void *location, uint64_t len_bytes);
void memdumpx64(void *location, uint64_t len_bytes);

void *memset(void *s, int c, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

#endif