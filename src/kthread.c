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
#include <stddef.h>
#include <kthread.h>
#include <scheduler.h>

/*
 * Terminates a kernel thread when its entry function returns normally.
 */
static void kthread_returned(void)
{
    scheduler_exit();
}

/*
 * Prepares a kernel thread's initial stack and execution context.
 *
 * The initial return address is placed where context_switch() expects
 * it. The stack is aligned for the x86-64 System V calling convention.
 *
 * The caller owns the stack allocation and must keep it alive for the
 * lifetime of the thread.
 */
bool kthread_prepare(thread_t *thread, void (*entry)(void), void *stack, uint64_t stack_size)
{
    if (thread == NULL || entry == NULL || stack == NULL || stack_size < 32) {
        return false;
    }

    uint64_t stack_top = (uint64_t)stack + stack_size;

    stack_top &= ~0xFULL;

    uint64_t *sp = (uint64_t *)stack_top;

    *--sp = (uint64_t)kthread_returned;
    *--sp = (uint64_t)entry;

    thread->context.rsp = (uint64_t)sp;
    thread->context.rbx = 0;
    thread->context.rbp = 0;
    thread->context.r12 = 0;
    thread->context.r13 = 0;
    thread->context.r14 = 0;
    thread->context.r15 = 0;

    thread->kernel_stack_top = stack_top;

    return true;
}