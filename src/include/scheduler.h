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
#ifndef _BLOREOS_SCHEDULER_H
#define _BLOREOS_SCHEDULER_H

#include <stdbool.h>
#include <thread.h>

#define SCHEDULER_MAX_THREADS 16

void        scheduler_init(void);
bool        scheduler_add(thread_t *thread);
thread_t    *scheduler_next(void);
thread_t    *scheduler_current(void);
void        scheduler_set_current(thread_t *thread);
void        scheduler_yield(void);
void        scheduler_exit(void) __attribute__((noreturn));
uint32_t    scheduler_reap(void);

#endif