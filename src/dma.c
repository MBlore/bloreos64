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

#include <dma.h>
#include <mem.h>

bool dma_alloc(size_t size, dma_buffer_t *buffer)
{
    if (buffer == NULL || size == 0) {
        return false;
    }

    // Calculate the required number of physical pages.
    size_t page_count = size / PAGE_SIZE;

    if (size % PAGE_SIZE != 0) {
        page_count++;
    }

    // Allocate physically contiguous memory.
    void *address = kpalloc(page_count);

    if (address == NULL) {
        return false;
    }

    // Zero all allocated pages before exposing them to hardware.
    memset(address, 0, page_count * PAGE_SIZE);

    // Populate the DMA buffer descriptor.
    buffer->virt_addr = address;
    buffer->phys_addr = (uint64_t)address - vmm_higher_half_offset;
    buffer->size = size;

    return true;
}

void dma_free(dma_buffer_t *buffer)
{
    if (buffer == NULL || buffer->virt_addr == NULL) {
        return;
    }

    // Release the underlying physical memory allocation.
    kfree(buffer->virt_addr);

    // Reset the buffer descriptor to prevent accidental reuse.
    buffer->virt_addr = NULL;
    buffer->phys_addr = 0;
    buffer->size = 0;
}