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
#ifndef _BLOREOS_ATOMIC_H
#define _BLOREOS_ATOMIC_H

#include <stdint.h>

typedef struct {
    uint8_t lock;
} spinlock_t;

static void spinlock_lock(spinlock_t *pLock)
{
    while (__sync_lock_test_and_set(&pLock->lock, 1)) {
        // Idle until locked.
    }
}

static void spinlock_unlock(spinlock_t *pLock)
{
    __sync_lock_release(&pLock->lock);
}

#endif