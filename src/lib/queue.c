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
#include <slob.h>
#include <stdbool.h>
#include <str.h>
#include <kernel.h>
#include <atomic.h>
#include <cpu.h>

/*
 * Creates a new cqueue with the specified internal buffer length.
 * Try to keep the len < the memory page size of 4096 bytes, which
 * would mean that len at maximum would be 1017 to fit in to 1 page.
*/
CQueue_t *cqueue_create(uint32_t len)
{
    if (len == 0) {
        return NULL;
    }

    size_t allocation_size = sizeof(CQueue_t) + sizeof(uint32_t) * (size_t)len;
    CQueue_t *q = slob_malloc(allocation_size);

    if (q == NULL) {
        return NULL;
    }

    q->buff = (uint32_t *)((uint8_t *)q + sizeof(CQueue_t));
    q->len = len;
    q->read_i = 0;
    q->write_i = 0;
    q->num_items = 0;
    q->lock.lock = 0;

    return q;
}

/*
 * Adds an item to the circular queue.
 * Disables local interrupts while holding the lock so an interrupt
 * handler cannot interrupt the lock owner and deadlock.
 */
bool cqueue_write(CQueue_t *q, uint32_t val)
{
    bool interrupts_enabled = set_interrupt_state(false);

    spinlock_lock(&q->lock);

    if (q->num_items == q->len) {
        spinlock_unlock(&q->lock);
        set_interrupt_state(interrupts_enabled);
        return false;
    }

    q->buff[q->write_i] = val;
    q->write_i++;

    if (q->write_i == q->len) {
        q->write_i = 0;
    }

    q->num_items++;

    spinlock_unlock(&q->lock);
    set_interrupt_state(interrupts_enabled);

    return true;
}

/*
 * Removes an item from the circular queue.
 * Preserves the caller's interrupt state while protecting queue access.
 */
uint32_t cqueue_read(CQueue_t *q)
{
    bool interrupts_enabled = set_interrupt_state(false);

    spinlock_lock(&q->lock);

    if (q->num_items == 0) {
        spinlock_unlock(&q->lock);
        set_interrupt_state(interrupts_enabled);

        kprintf("FATAL: Attempted to dequeue from an empty queue.\n");
        hcf();
        return 0;
    }

    uint32_t val = q->buff[q->read_i];

    q->read_i++;

    if (q->read_i == q->len) {
        q->read_i = 0;
    }

    q->num_items--;

    spinlock_unlock(&q->lock);
    set_interrupt_state(interrupts_enabled);

    return val;
}