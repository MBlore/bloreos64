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

#ifndef _BLOREOS_DMA_H
#define _BLOREOS_DMA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct
{
    void *virt_addr;
    uint64_t phys_addr;
    size_t size;
} dma_buffer_t;

// Allocate a physically contiguous, page-aligned DMA buffer.
// Returns false if the allocation fails.
bool dma_alloc(size_t size, dma_buffer_t *buffer);

// Release a DMA buffer.
// The device must no longer be accessing the buffer.
void dma_free(dma_buffer_t *buffer);

#endif