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
    A really simple forward only, no freeing, memory allocator.
    The fact that this can't free up memory is bad, but if you perform allocations
    over the page size, all the existing memory remaining in the current page is
    skipped and never used.

    With that said, this IS better than just allocating pages using kpalloc/kalloc as
    it saves you constantly allocating pages per call and lets you consume smaller
    chunks inside a single page.
*/
#include <bump.h>
#include <str.h>
#include <mem.h>
#include <vm.h>
#include <math.h>

// State for tracking where we are in the page we have allocated.
void *_cursor;
uint16_t _offset;       // 0 is the start of the page, 4094 is the last byte in the page.

void *bump_malloc(size_t size)
{
    // How many pages are we requesting?
    uint64_t numPages = DIV_ROUNDUP(size, PAGE_SIZE);
    
    if (numPages > 1) {
        // We need a batch of allocated pages to fit this request.
        // We can't include the current page at all because it may not be
        // contiguous with the next pages we request from the PM.
        void *startAddr = kpalloc(numPages);
        
        // Offset in to the last page of the chunk.
        _offset = (numPages * PAGE_SIZE) - size;

        // Set the cursor to the next free byte in the last page.
        _cursor = (char*)startAddr + size;

        return startAddr;
    }

    // First allocation.
    if (_cursor == 0) {
        _cursor = kpalloc(1);
        _offset = 0;
    }

    // If the request doesnt fit inside the remaining page space, get a new page.
    if (size > (size_t)PAGE_SIZE - _offset) {
        _cursor = kpalloc(1);
        _offset = 0;
    }

    // Now reserve the space from the pointed to page and advance the cursor.
    void *startAddr = _cursor;
    _cursor = (char*)_cursor + size;
    _offset += size;

    return startAddr;
}

void bump_free(void *ptr)
{
    (void)ptr;
    // No frees! Muhahah.
}