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
#include <stddef.h>
#include <scheduler.h>
#include <cpu.h>
#include <thread.h>

static thread_t *threads[SCHEDULER_MAX_THREADS];
static thread_t *current_thread;
static uint32_t thread_count;
static uint32_t next_index;

/*
 * Initialises the cooperative scheduler.
 *
 * All scheduling operations currently execute on a single CPU.
 * No context switching or timer preemption is performed.
 */
void scheduler_init(void)
{
    for (uint32_t i = 0; i < SCHEDULER_MAX_THREADS; i++) {
        threads[i] = NULL;
    }

    current_thread = NULL;
    thread_count = 0;
    next_index = 0;
}

/*
 * Registers a thread with the scheduler.
 *
 * The caller owns the thread storage and must keep it alive while registered.
 */
bool scheduler_add(thread_t *thread)
{
    if (thread == NULL || thread_count >= SCHEDULER_MAX_THREADS) {
        return false;
    }

    if (thread->state != THREAD_READY) {
        return false;
    }

    for (uint32_t i = 0; i < thread_count; i++) {
        if (threads[i] == thread) {
            return false;
        }
    }

    threads[thread_count++] = thread;
    return true;
}

/*
 * Selects the next ready thread using round-robin ordering.
 *
 * This is selection only. It does not save registers, switch stacks,
 * change address spaces or transfer execution to the selected thread.
 */
thread_t *scheduler_next(void)
{
    if (thread_count == 0) {
        return NULL;
    }

    for (uint32_t i = 0; i < thread_count; i++) {
        uint32_t index = next_index;
        next_index = (next_index + 1) % thread_count;

        thread_t *thread = threads[index];

        if (thread->state == THREAD_READY) {
            return thread;
        }
    }

    return NULL;
}

/*
 * Returns the most recently selected thread.
 *
 * This pointer does not yet imply that the CPU is executing that thread.
 */
thread_t *scheduler_current(void)
{
    return current_thread;
}

/*
 * Registers the thread currently executing on this CPU.
 *
 * Used initially to associate the bootstrap kernel execution context
 * with a thread record. This does not perform a context switch.
 */
void scheduler_set_current(thread_t *thread)
{
    current_thread = thread;
}

/*
 * Cooperatively yields execution to the next runnable thread.
 *
 * Interrupts remain disabled across the context switch. Each suspended
 * invocation restores its own previous interrupt state when resumed.
 *
 * This implementation supports one CPU and kernel threads only.
 */
void scheduler_yield(void)
{
    bool interrupts_enabled = interrupt_state();

    disable_interrupts();

    thread_t *previous = current_thread;

    if (previous == NULL) {
        set_interrupt_state(interrupts_enabled);
        return;
    }

    if (previous->state == THREAD_RUNNING) {
        previous->state = THREAD_READY;
    }

    thread_t *next = scheduler_next();

    if (next == NULL) {
        previous->state = THREAD_RUNNING;
        current_thread = previous;
        set_interrupt_state(interrupts_enabled);
        return;
    }

    if (next == previous) {
        previous->state = THREAD_RUNNING;
        current_thread = previous;
        set_interrupt_state(interrupts_enabled);
        return;
    }

    next->state = THREAD_RUNNING;
    current_thread = next;

    context_switch(&previous->context, &next->context);

    set_interrupt_state(interrupts_enabled);
}