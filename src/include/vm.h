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
#ifndef _BLOREOS_VM_H
#define _BLOREOS_VM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Page mapping permission flags.
#define PAGE_RW   (1ULL << 1)
#define PAGE_USER (1ULL << 2)

// Page memory type selection flags.
// These three make up the selection index in the PAT from a PTE.
#define PAGE_PWT (1ULL << 3)
#define PAGE_PCD (1ULL << 4)
#define PAGE_PAT (1ULL << 7)
#define PAGE_NX (1ULL << 63)

#define USER_CODE_BASE  0x0000000000400000ULL
#define USER_STACK_TOP  0x0000000000800000ULL
#define USER_STACK_SIZE (4 * 4096)

// Architectural PAT memory type encodings.
typedef enum {
    VM_MEMORY_UC       = 0x00,
    VM_MEMORY_WC       = 0x01,
    VM_MEMORY_WT       = 0x04,
    VM_MEMORY_WP       = 0x05,
    VM_MEMORY_WB       = 0x06,
    VM_MEMORY_UC_MINUS = 0x07
} vm_memory_type_t;

// Obtain the page mapping flags for a requested PAT memory type.
// Returns false if the memory type is unavailable.
// On failure, the output flags remain unchanged.
bool vm_get_memory_type_flags(vm_memory_type_t type,
                              uint64_t *flags);

void vm_init();

// Translate a virtual address into a physical address.
// Returns false on failure, leaving phys_addr unchanged.
bool vm_translate(uint64_t virt_addr, uint64_t *phys_addr);

// Map a virtual page to a physical page using the specified flags.
// Both addresses must be 4 KiB aligned.
// Returns false on failure or if a mapping already exists.
bool vm_map_page(uint64_t virt_addr,
                 uint64_t phys_addr,
                 uint64_t flags);

bool vm_map_user_page(uint64_t virt_addr, uint64_t phys_addr, bool writable);

// Remove a 4 KiB virtual memory mapping.
// The virtual address must be page-aligned.
// The mapped physical page is not released.
// Returns false if the mapping cannot be removed.
bool vm_unmap_page(uint64_t virt_addr);

// Map a physical MMIO region into kernel virtual address space.
// Returns a virtual address on success, or NULL on failure.
void *vm_map_mmio(uint64_t phys_addr, size_t size);

bool vm_create_address_space(uint64_t *page_table_root);

bool vm_map_page_in(uint64_t page_table_root, uint64_t virt_addr,
                    uint64_t phys_addr, uint64_t flags);

bool vm_unmap_page_in(uint64_t page_table_root, uint64_t virt_addr);

bool vm_destroy_address_space(uint64_t page_table_root);

bool vm_nx_supported(void);

uint64_t vm_get_address_space(void);
bool vm_switch_address_space(uint64_t page_table_root);

bool vm_translate_user(uint64_t virt_addr, uint64_t *phys_addr);

#endif