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
#include <queue.h>
#include <mem.h>
#include <stdbool.h>
#include <str.h>
#include <kernel.h>
#include <atomic.h>

/*
 * Creates a new cqueue with the specified internal buffer length.
 * Try to keep the len < the memory page size of 4096 bytes, which
 * would mean that len at maximum would be 1017 to fit in to 1 page.
*/
CQueue_t* cqueue_create(uint32_t len)
{
    // Allocate the struct and buffer next to each other to ensure
    // at least a single page is used in the PMM.
    char *mem = kalloc(sizeof(CQueue_t) + (sizeof(uint32_t) * len));

    CQueue_t *q = (CQueue_t*)mem;
    uint32_t *buff = (uint32_t*)&mem[sizeof(CQueue_t)];
    q->buff = buff;
    q->len = len;
    q->read_i = 0;
    q->write_i = 0;
    q->num_items = 0;
    return q;
}

/*
 * Adds the specified value to the buffer and advances the write cursor.
 * Returns true for successful add.
 * Returns false if the buffer is full and failed to add.
*/
bool cqueue_write(CQueue_t *q, uint32_t val)
{
    spinlock_lock(&q->lock);

    if (q->num_items == q->len) {
        // Overflow - we'll leave it up to the caller to decide what to do
        // with an overflowing buffer.
        spinlock_unlock(&q->lock);
        return false;
    }

    q->buff[q->write_i] = val;
    q->write_i++;

    if (q->write_i == q->len) {
        // Wrap around.
        q->write_i = 0;
    }

    spinlock_unlock(&q->lock);

    q->num_items++;

    return true;
}

/*
 * Reads an item from the queue and forwards the read cursor.
 * If 'num_items' is 0, this call will cause a CPU halt. 
*/
uint32_t cqueue_read(CQueue_t *q)
{
    spinlock_lock(&q->lock);

    if (q->num_items == 0) {
        kprintf("FATAL: Attempted to dequeue without first checking 'num_items' > 0.");
        hcf();
        return 0;
    }

    uint32_t val = q->buff[q->read_i];

    q->read_i++;

    if (q->read_i == q->len) {
        // Wrap around.
        q->read_i = 0;
    }

    q->num_items--;

    spinlock_unlock(&q->lock);

    return val;
}