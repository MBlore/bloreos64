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
/*
    An implementation of the SLOB algorithm.
    Merges sibling nodes when memory is freed to help with fragmentation.
*/
#include <slob.h>
#include <str.h>
#include <mem.h>

struct SlobEntry {
    struct SlobEntry   *pNext;
    uint64_t            base;
    size_t              length;
} __attribute__((packed));

struct SlobHeader {
    uint64_t            length;
} __attribute__((packed));

uint8_t _init = 0;
struct SlobEntry *pHead = 0;

/*
    Intializes the intial slob entries according to available areas of RAM in the Limine memory map.
*/
void slob_init()
{
    int numEntries = 0;

    // How many entries will we need to create?
    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            numEntries++;
        }
    }

    // Allocate enough room for all the initial slob entries.
    pHead = (struct SlobEntry*)memmap_alloc(sizeof(struct SlobEntry) * numEntries);
    
    // Initialize the entries.
    int entryIndex = 0;

    for (size_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            pHead[entryIndex].length = entry->length;
            pHead[entryIndex].base = entry->base;

            if (entryIndex < numEntries-1)
                pHead[entryIndex].pNext = &pHead[entryIndex+1];
            else
                pHead[entryIndex].pNext = 0;    // End entry has no next.

            entryIndex++;
        }
    }
}

/*
    Allocates a new slob entry in the first available slob entry we can find that fits our new
    allocation size. We split the found entry and insert our new entry at the start of it.
*/
void *slob_malloc(size_t size)
{
    if (_init == 0) {
        slob_init();
        _init = 1;
    }
    
    struct SlobEntry *pNext = pHead;
    while(pNext != 0) {

        if (size + sizeof(struct SlobHeader) <= pNext->length) {
            // Found an entry that can fit our request, lets split the node.
            uint64_t totalSize = sizeof(struct SlobHeader) + size;
            
            // Setup the new chunk thats now split from the existing chunk that we've found.
            struct SlobHeader *pHeader = (struct SlobHeader*)(pNext->base + vmm_higher_half_offset);
            pHeader->length = totalSize;

            // Get the users memory location after the slob entry header.
            void *pAlloc = (void*)(pNext->base + sizeof(struct SlobHeader) + vmm_higher_half_offset);

            // Move the base of the entry up by our new allocation size.
            pNext->base += totalSize;
            pNext->length -= totalSize;
            
            return pAlloc;
        }

        pNext = pNext->pNext;
    }

    kprintf("No memory available!\n");
    return 0;
}

/*
    Free takes the allocated entry and makes it available by creating a new slob entry and pre-pending it to the
    list of free entries.

    Over-time, theres going to be a lot of small available entries as memory is freed until we implement
    merging/defragging of the entries.
*/
void slob_free(void *ptr)
{
    // TODO: Need a spinklock to protect the structure change for multi-core.

    // Grab the header from the previous bytes of this memory location.
    struct SlobHeader *pHeader = (struct SlobHeader*)(ptr - sizeof(struct SlobHeader));

    // Create the new entry.
    struct SlobEntry *pEntry = (struct SlobEntry*)slob_malloc(sizeof(struct SlobEntry));
    pEntry->base = (uint64_t)pHeader - vmm_higher_half_offset;
    pEntry->length = pHeader->length;

    // Pre-pend the new entry to the list.
    pEntry->pNext = pHead;
    pHead = pEntry;
}