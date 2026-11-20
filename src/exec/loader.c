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
#include <mem.h>
#include <vm.h>

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
 * Checks whether two ELF segments share a 4 KiB virtual page.
 *
 * Segments may occupy different byte ranges while still requiring
 * the same physical page after page alignment.
 */
static bool elf_segments_share_page(const elf_load_segment_t *a, const elf_load_segment_t *b)
{
    if (a->memory_size == 0 || b->memory_size == 0) {
        return false;
    }

    const uint64_t a_start = a->virtual_address & ~(PAGE_SIZE - 1ULL);
    const uint64_t b_start = b->virtual_address & ~(PAGE_SIZE - 1ULL);

    // Use inclusive final-page addresses to avoid rounding past the user limit.
    const uint64_t a_last = (a->virtual_address + a->memory_size - 1) & ~(PAGE_SIZE - 1ULL);
    const uint64_t b_last = (b->virtual_address + b->memory_size - 1) & ~(PAGE_SIZE - 1ULL);

    return a_start <= b_last && b_start <= a_last;
}

/*
 * Rejects shared pages requiring incompatible writable and
 * executable permissions.
 *
 * A shared page is rejected whenever either segment requires
 * write access and either segment requires execute access.
 */
static bool elf_segments_have_wx_conflict(const elf_load_segment_t *a, const elf_load_segment_t *b)
{
    if (!elf_segments_share_page(a, b)) {
        return false;
    }

    const uint32_t combined_flags = a->flags | b->flags;

    return (combined_flags & (ELF_PF_W | ELF_PF_X)) == (ELF_PF_W | ELF_PF_X);
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

        // Reject overlapping segments and incompatible shared-page permissions.
        for (uint16_t j = 0; j < result.segment_count; j++) {
            const elf_load_segment_t *previous = &result.segments[j];

            if (elf_segments_overlap(destination, previous)) {
                return false;
            }

            if (elf_segments_have_wx_conflict(destination, previous)) {
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

/*
 * Calculates the required page permissions across all ELF segments
 * occupying a given 4 KiB page.
 *
 * Rejects pages that would be both writable and executable.
 */
static bool elf_loader_page_flags(const elf_load_plan_t *plan, uint64_t address, uint64_t *flags)
{
    bool writable = false;
    bool executable = false;

    for (uint16_t i = 0; i < plan->segment_count; i++) {
        const elf_load_segment_t *segment = &plan->segments[i];

        if (segment->memory_size == 0) {
            continue;
        }

        const uint64_t segment_start = segment->virtual_address;
        const uint64_t segment_end = segment_start + segment->memory_size;

        if (segment_start >= address + PAGE_SIZE || segment_end <= address) {
            continue;
        }

        writable |= (segment->flags & ELF_PF_W) != 0;
        executable |= (segment->flags & ELF_PF_X) != 0;
    }

    if (writable && executable) {
        return false;
    }

    *flags = PAGE_USER;

    if (writable) {
        *flags |= PAGE_RW;
    }

    if (!executable) {
        *flags |= PAGE_NX;
    }

    return true;
}

/*
 * Allocates physical memory for all loadable ELF segments.
 *
 * Each segment is rounded to 4 KiB page boundaries. Existing process
 * pages are reused when two segments share a page boundary.
 *
 * The caller owns cleanup of any pages allocated before a failure.
 */
bool elf_loader_allocate_segments(process_t *process, const elf_load_plan_t *plan)
{
    if (process == NULL || plan == NULL || process->page_table_root == 0) {
        return false;
    }

    if (plan->segment_count > ELF_LOADER_MAX_SEGMENTS) {
        return false;
    }

    for (uint16_t i = 0; i < plan->segment_count; i++) {
        const elf_load_segment_t *segment = &plan->segments[i];

        if (segment->memory_size == 0) {
            continue;
        }

        if (segment->virtual_address < ELF_USER_MIN_ADDRESS ||
            segment->virtual_address >= ELF_USER_MAX_ADDRESS ||
            segment->memory_size > ELF_USER_MAX_ADDRESS - segment->virtual_address) {
            return false;
        }

        const uint64_t start = segment->virtual_address & ~(PAGE_SIZE - 1ULL);
        const uint64_t end = segment->virtual_address + segment->memory_size;

        for (uint64_t address = start; address < end;) {
            if (process_find_page(process, address) == NULL) {
                uint64_t flags;

                if (!elf_loader_page_flags(plan, address, &flags)) {
                    return false;
                }

                if (!process_alloc_page(process, address, flags)) {
                    return false;
                }
            }

            address += PAGE_SIZE;
        }
    }

    return true;
}

/*
 * Copies the file-backed contents of ELF segments into process memory.
 */
bool elf_loader_copy_segments(process_t *process, const void *data, size_t size,
                              const elf_load_plan_t *plan)
{
    if (process == NULL || data == NULL || plan == NULL || process->page_table_root == 0) {
        return false;
    }

    if (plan->segment_count > ELF_LOADER_MAX_SEGMENTS) {
        return false;
    }

    const uint8_t *image = data;

    for (uint16_t i = 0; i < plan->segment_count; i++) {
        const elf_load_segment_t *segment = &plan->segments[i];

        if (segment->file_size > segment->memory_size ||
            segment->file_offset > size ||
            segment->file_size > size - segment->file_offset) {
            return false;
        }

        if (segment->virtual_address < ELF_USER_MIN_ADDRESS ||
            segment->virtual_address >= ELF_USER_MAX_ADDRESS ||
            segment->memory_size > ELF_USER_MAX_ADDRESS - segment->virtual_address) {
            return false;
        }

        uint64_t copied = 0;

        while (copied < segment->file_size) {
            const uint64_t address = segment->virtual_address + copied;
            const size_t page_offset = (size_t)(address & (PAGE_SIZE - 1ULL));

            size_t bytes = PAGE_SIZE - page_offset;

            if ((uint64_t)bytes > segment->file_size - copied) {
                bytes = (size_t)(segment->file_size - copied);
            }

            uint8_t *page = process_find_page(process, address);

            if (page == NULL) {
                return false;
            }

            memcpy(page + page_offset, image + segment->file_offset + copied, bytes);

            copied += bytes;
        }
    }

    return true;
}

/*
 * Validates and loads an ELF64 executable into an existing process.
 *
 * The process must have a valid, empty address space and no owned pages.
 *
 * On success, the process owns all allocated segment pages and the
 * executable entry point is returned through entry_point.
 *
 * On failure, all pages allocated during loading are released.
 * The caller remains responsible for destroying the address space.
 */
bool elf_loader_load(process_t *process, const void *data, size_t size, uint64_t *entry_point)
{
    if (process == NULL || data == NULL || entry_point == NULL) {
        return false;
    }

    if (process->page_table_root == 0 || process->owned_pages != NULL) {
        return false;
    }

    // User executables require NX to enforce non-executable data and stack pages.
    if (!vm_nx_supported()) {
        return false;
    }

    elf_load_plan_t plan;

    // Validate the executable before allocating any process memory.
    if (!elf_loader_plan(data, size, &plan)) {
        return false;
    }

    // Allocate physical pages for all loadable segments.
    if (!elf_loader_allocate_segments(process, &plan)) {
        process_free_pages(process);
        return false;
    }

    // Copy file-backed segment contents into process memory.
    if (!elf_loader_copy_segments(process, data, size, &plan)) {
        process_free_pages(process);
        return false;
    }

    // Allocate the initial Ring 3 stack.
    if (!elf_loader_allocate_stack(process)) {
        process_free_pages(process);
        return false;
    }

    // Loading completed successfully.
    *entry_point = plan.entry_point;

    return true;
}

/*
 * Allocates the initial user-mode stack for a process.
 *
 * The stack occupies USER_STACK_SIZE bytes immediately below
 * USER_STACK_TOP and is writable from Ring 3.
 *
 * The process must not already own pages in this address range.
 */
bool elf_loader_allocate_stack(process_t *process)
{
    if (process == NULL || process->page_table_root == 0) {
        return false;
    }

    const uint64_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;

    // Reject an invalid or non-page-aligned stack configuration.
    if (USER_STACK_SIZE == 0 ||
        (USER_STACK_SIZE & (PAGE_SIZE - 1ULL)) ||
        (USER_STACK_TOP & (PAGE_SIZE - 1ULL))) {
        return false;
    }

    // Ensure the stack does not overlap existing process allocations.
    for (uint64_t address = stack_bottom; address < USER_STACK_TOP; address += PAGE_SIZE) {
        if (process_find_page(process, address) != NULL) {
            return false;
        }
    }

    // Allocate and map each stack page as user-accessible and writable.
    for (uint64_t address = stack_bottom; address < USER_STACK_TOP; address += PAGE_SIZE) {
        if (!process_alloc_page(process, address, PAGE_USER | PAGE_RW | PAGE_NX)) {
            return false;
        }
    }

    // x86-64 stacks grow downwards, starting at the highest address.
    process->user_stack_pointer = USER_STACK_TOP;

    return true;
}