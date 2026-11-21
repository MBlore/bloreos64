/*
    BloreOS - Operating System
    Copyright (C) 2023 Martin Blore

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <slob.h>
#include <mem.h>
#include <atomic.h>

#define SLOB_ALIGNMENT 16
#define SLOB_MAGIC 0x534C4F424D454D31ULL
#define SLOB_BLOCK_FREE 0x46524545424C4F43ULL
#define SLOB_BLOCK_USED 0x55534544424C4F43ULL

typedef struct slob_block_t {
    size_t size;
    struct slob_block_t *next;
    uint64_t state;
    uint64_t reserved;
} slob_block_t;

typedef struct slob_page_t {
    struct slob_page_t *next;
    slob_block_t *free_list;
    size_t allocations;
    uint64_t magic;
} slob_page_t;

typedef struct slob_large_t {
    struct slob_large_t *next;
    size_t pages;
    uint64_t magic;
    uint64_t reserved;
} slob_large_t;

_Static_assert(sizeof(slob_block_t) % SLOB_ALIGNMENT == 0, "SLOB block alignment");
_Static_assert(sizeof(slob_page_t) % SLOB_ALIGNMENT == 0, "SLOB page alignment");
_Static_assert(sizeof(slob_large_t) % SLOB_ALIGNMENT == 0, "SLOB large alignment");

static slob_page_t *slob_pages = NULL;
static slob_large_t *slob_large = NULL;
static spinlock_t slob_lock = {0};

/*
 * Initializes the allocator's page lists.
 *
 * The allocator starts empty and requests physical pages on demand.
 * Must be called once during kernel initialization before allocation.
 */
void slob_init(void)
{
    slob_pages = NULL;
    slob_large = NULL;
    slob_lock.lock = 0;
}

/*
 * Creates a SLOB page containing one initially free block.
 */
static slob_page_t *slob_create_page(void)
{
    slob_page_t *page = kpalloc(1);

    if (page == NULL) {
        return NULL;
    }

    page->next = NULL;
    page->allocations = 0;
    page->magic = SLOB_MAGIC;

    slob_block_t *block = (slob_block_t *)((uint8_t *)page + sizeof(slob_page_t));

    block->size = PAGE_SIZE - sizeof(slob_page_t);
    block->next = NULL;
    block->state = SLOB_BLOCK_FREE;
    block->reserved = 0;

    page->free_list = block;

    return page;
}

/*
 * Allocates an aligned block from an existing SLOB page.
 *
 * Splits a free block when the remainder is large enough to hold
 * another block header and at least one aligned allocation unit.
 */
static void *slob_allocate_from_page(slob_page_t *page, size_t required)
{
    slob_block_t *previous = NULL;
    slob_block_t *block = page->free_list;

    while (block != NULL) {
        if (block->size >= required) {
            size_t remaining = block->size - required;

            if (remaining >= sizeof(slob_block_t) + SLOB_ALIGNMENT) {
                slob_block_t *remainder = (slob_block_t *)((uint8_t *)block + required);

                remainder->size = remaining;
                remainder->next = block->next;
                remainder->state = SLOB_BLOCK_FREE;
                remainder->reserved = 0;

                if (previous != NULL) {
                    previous->next = remainder;
                } else {
                    page->free_list = remainder;
                }

                block->size = required;
            } else {
                if (previous != NULL) {
                    previous->next = block->next;
                } else {
                    page->free_list = block->next;
                }
            }

            block->next = NULL;
            block->state = SLOB_BLOCK_USED;
            page->allocations++;

            return (uint8_t *)block + sizeof(slob_block_t);
        }

        previous = block;
        block = block->next;
    }

    return NULL;
}

/*
 * Allocates an object larger than the capacity of a single SLOB page.
 *
 * Large objects receive contiguous pages directly from the PMM.
 * Their allocation headers are tracked separately for reclamation.
 */
static void *slob_allocate_large(size_t size)
{
    if (size > SIZE_MAX - sizeof(slob_large_t)) {
        return NULL;
    }

    size_t total = size + sizeof(slob_large_t);
    size_t pages = total / PAGE_SIZE;

    if (total % PAGE_SIZE != 0) {
        pages++;
    }

    slob_large_t *allocation = kpalloc(pages);

    if (allocation == NULL) {
        return NULL;
    }

    allocation->pages = pages;
    allocation->magic = SLOB_MAGIC;
    allocation->reserved = 0;

    allocation->next = slob_large;
    slob_large = allocation;

    return (uint8_t *)allocation + sizeof(slob_large_t);
}

/*
 * Allocates a 16-byte-aligned kernel object.
 *
 * Small objects are allocated from SLOB-managed pages. Larger objects
 * receive contiguous pages directly from the physical memory manager.
 *
 * Returns NULL on invalid size or allocation failure.
 */
void *slob_malloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    if (size > SIZE_MAX - sizeof(slob_block_t) - (SLOB_ALIGNMENT - 1)) {
        return NULL;
    }

    size_t required = size + sizeof(slob_block_t);
    required = (required + SLOB_ALIGNMENT - 1) & ~(size_t)(SLOB_ALIGNMENT - 1);

    spinlock_lock(&slob_lock);

    if (required > PAGE_SIZE - sizeof(slob_page_t)) {
        void *result = slob_allocate_large(size);
        spinlock_unlock(&slob_lock);
        return result;
    }

    for (slob_page_t *page = slob_pages; page != NULL; page = page->next) {
        void *result = slob_allocate_from_page(page, required);

        if (result != NULL) {
            spinlock_unlock(&slob_lock);
            return result;
        }
    }

    slob_page_t *page = slob_create_page();

    if (page == NULL) {
        spinlock_unlock(&slob_lock);
        return NULL;
    }

    page->next = slob_pages;
    slob_pages = page;

    void *result = slob_allocate_from_page(page, required);

    spinlock_unlock(&slob_lock);
    return result;
}

/*
 * Inserts a released block into a page's address-ordered free list.
 *
 * Adjacent free blocks are merged in both directions to reduce
 * fragmentation without allocating additional metadata.
 */
static void slob_release_block(slob_page_t *page, slob_block_t *block)
{
    slob_block_t *previous = NULL;
    slob_block_t *current = page->free_list;

    while (current != NULL && (uintptr_t)current < (uintptr_t)block) {
        previous = current;
        current = current->next;
    }

    block->state = SLOB_BLOCK_FREE;
    block->next = current;

    if (previous != NULL) {
        previous->next = block;
    } else {
        page->free_list = block;
    }

    if (current != NULL && (uint8_t *)block + block->size == (uint8_t *)current) {
        block->size += current->size;
        block->next = current->next;
    }

    if (previous != NULL && (uint8_t *)previous + previous->size == (uint8_t *)block) {
        previous->size += block->size;
        previous->next = block->next;
    }
}

/*
 * Releases a previously allocated SLOB object.
 *
 * Empty SLOB pages are returned to the PMM. Large allocations are
 * released directly through the PMM using their recorded page counts.
 *
 * NULL is a no-op. Other pointers must come from slob_malloc().
 */
void slob_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }

    spinlock_lock(&slob_lock);

    slob_large_t *previous_large = NULL;
    slob_large_t *large = slob_large;

    while (large != NULL) {
        if ((uint8_t *)large + sizeof(slob_large_t) == (uint8_t *)ptr) {
            if (previous_large != NULL) {
                previous_large->next = large->next;
            } else {
                slob_large = large->next;
            }

            kfree(large);
            spinlock_unlock(&slob_lock);
            return;
        }

        previous_large = large;
        large = large->next;
    }

    slob_page_t *previous_page = NULL;
    slob_page_t *page = slob_pages;

    while (page != NULL) {
        uintptr_t start = (uintptr_t)page + sizeof(slob_page_t);
        uintptr_t end = (uintptr_t)page + PAGE_SIZE;
        uintptr_t address = (uintptr_t)ptr;

        if (address >= start + sizeof(slob_block_t) && address < end) {
            slob_block_t *block = (slob_block_t *)((uint8_t *)ptr - sizeof(slob_block_t));

            if (block->state != SLOB_BLOCK_USED || page->allocations == 0) {
                spinlock_unlock(&slob_lock);
                return;
            }

            block->state = SLOB_BLOCK_FREE;
            page->allocations--;

            if (page->allocations == 0) {
                if (previous_page != NULL) {
                    previous_page->next = page->next;
                } else {
                    slob_pages = page->next;
                }

                kfree(page);
            } else {
                slob_release_block(page, block);
            }

            spinlock_unlock(&slob_lock);
            return;
        }

        previous_page = page;
        page = page->next;
    }

    spinlock_unlock(&slob_lock);
}