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
#include <thread.h>
#include <mem.h>
#include <string.h>

/*
 * Allocates and initialises a thread structure.
 *
 * The thread belongs to the specified process, but does not own it.
 * Stack allocation and execution context setup are handled separately.
 *
 * The caller must eventually release the thread using thread_destroy().
 */
thread_t *thread_create(process_t *process)
{
    thread_t *thread = malloc(sizeof(thread_t));

    if (thread == NULL) {
        return NULL;
    }

    memset(thread, 0, sizeof(thread_t));

    thread->process = process;
    thread->state = THREAD_READY;

    thread->kernel_stack = kpalloc(THREAD_KERNEL_STACK_SIZE / PAGE_SIZE);

    if (thread->kernel_stack == NULL) {
        free(thread);
        return NULL;
    }

    thread->kernel_stack_top = (uint64_t)thread->kernel_stack + THREAD_KERNEL_STACK_SIZE;

    return thread;
}

/*
 * Releases a dynamically allocated thread structure.
 *
 * The thread must no longer be running or registered with the scheduler.
 * The thread does not own its process or execution stacks.
 *
 * Only threads allocated by thread_create() may be destroyed here.
 */
void thread_destroy(thread_t *thread)
{
    if (thread == NULL) {
        return;
    }

    if (thread->kernel_stack != NULL) {
        kfree(thread->kernel_stack);
    }

    free(thread);
}