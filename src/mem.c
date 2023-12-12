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
#include <stdarg.h>
#include <stddef.h>
#include <mem.h>
#include <string.h>
#include <limine.h>
#include <str.h>
#include <atomic.h>
#include <math.h>
#include <bitmap.h>

spinlock_t lock = {0};

uint64_t max_pages_available;
uint64_t total_memory_bytes;

volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0};

volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

volatile struct limine_memmap_response *memmap;
volatile struct limine_hhdm_response *hhdm;

static uint64_t highest_address = 0;
static uint64_t lowest_address = 0;
static uint64_t num_pages_in_map = 0;
static uint8_t *page_bitmap = NULL;

// GCC and Clang reserve the right to generate calls to the following
// 4 functions even if they are not directly called.
// Implement them as the C specification mandates.
// DO NOT remove or rename these functions, or stuff will eventually break!
void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    for (size_t i = 0; i < n; i++)
    {
        pdest[i] = psrc[i];
    }

    return dest;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;

    for (size_t i = 0; i < n; i++)
    {
        p[i] = (uint8_t)c;
    }

    return s;
}

void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *pdest = (uint8_t *)dest;
    const uint8_t *psrc = (const uint8_t *)src;

    if (src > dest)
    {
        for (size_t i = 0; i < n; i++)
        {
            pdest[i] = psrc[i];
        }
    }
    else if (src < dest)
    {
        for (size_t i = n; i > 0; i--)
        {
            pdest[i - 1] = psrc[i - 1];
        }
    }

    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++)
    {
        if (p1[i] != p2[i])
        {
            return p1[i] < p2[i] ? -1 : 1;
        }
    }

    return 0;
}

/*
    Initialize the physical memory manager.
*/
void kmem_init()
{
    memmap = memmap_request.response;
    hhdm = hhdm_request.response;

    kprintf("Initialzing PMM...\n");

    // Gather some stats about the memory map so we know how big we need to make our
    // page bitmap.
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            kprintf("Memory Map Entry %d Base: %lu\n", i, entry->base);
            kprintf("Memory Map Entry %d Length: %lu\n", i, entry->length);
            total_memory_bytes += entry->length;

            // In this entry, how many pages are there?
            max_pages_available += entry->length / PAGE_SIZE;

            highest_address = MAX(highest_address, entry->base + entry->length);

            if (lowest_address == 0) {
                lowest_address = entry->base;
            } else {
                lowest_address = MIN(lowest_address, entry->base);
            }
        }
    }

    uint64_t map_size_bytes = highest_address - lowest_address;
    num_pages_in_map = map_size_bytes / PAGE_SIZE;
    
    // Convert page numbers (bits) to bytes (8 bits).
    // The block size for the bitmap aligns to the page size even though we might not need that much.
    uint64_t bitmap_size = ALIGN_UP(num_pages_in_map / 8, PAGE_SIZE);

    kprintf("Total Memory: %lu Mib\n", total_memory_bytes / 1024 / 1024);
    kprintf("Total Map Pages: %lu\n", num_pages_in_map);
    kprintf("Total Pages Available: %lu\n", max_pages_available);
    kprintf("Page Bitmap Size: %lu Kib\n", bitmap_size / 1024);

    kprintf("Locating space for bitmap...\n");

    // Now we know how big our page map needs to be, let's find a spare place in memory
    // to keep it.
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        // Is this entry big enough to contain our bitmap?
        if (entry->length >= bitmap_size) {
            // We've got a spot, lets point there.
            // We have to use the HHDM offset to correctly point to this physical place in virtual memory.
            page_bitmap = (uint8_t*)(entry->base + hhdm->offset);

            // Now set all the bits to 1 in the map to mark everything as taken to start with.
            memset(page_bitmap, 0xFF, bitmap_size);

            // Change the values in the limine map as this part of mem is now permanently allocated to our kernel.
            entry->length -= bitmap_size;
            entry->base += bitmap_size;

            break;
        }
    }

    kprintf("Searching for free memory pages...\n");

    // So we have a memory map allocated and its all full. Let's walk through the memory map entries again
    // and start making pages available.
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        // Find the page this entry refers to in our bitmap.
        uint64_t start_bit = (entry->base - lowest_address) / PAGE_SIZE;
        uint64_t pages_free = entry->length / PAGE_SIZE;

        for (uint64_t i = start_bit; i < start_bit + pages_free; i++) {
            bitmap_off(page_bitmap, i);
        }
    }

    kprintf("PMM initialized.\n");
}

void pmm_alloc()
{
    spinlock_lock(&lock);

    // Grab a page here...

    spinlock_unlock(&lock);
}