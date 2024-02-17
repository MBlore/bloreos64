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
#ifndef _BLOREOS_QUEUE_H
#define _BLOREOS_QUEUE_H

#include <stdbool.h>
#include <atomic.h>

// Circular queue.
typedef struct {
    uint32_t read_i;
    uint32_t write_i;
    uint32_t *buff;
    uint32_t len;
    uint32_t num_items;
    spinlock_t lock;
} __attribute__((packed)) CQueue_t;

CQueue_t*   cqueue_create(uint32_t len);
bool        cqueue_write(CQueue_t *q, uint32_t val);
uint32_t    cqueue_read(CQueue_t *q);

#endif