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
#include <process.h>
#include <thread.h>
#include <vm.h>

/*
 * Initialises the bootstrap user process and its first thread.
 *
 * This is temporary single-process plumbing. Process allocation,
 * lifetime management and scheduling will be introduced separately.
 */
void process_init_test(process_t *process, thread_t *thread)
{
    process->pid = 1;
    process->page_table_root = 0;

    thread->tid = 1;
    thread->process = process;
    thread->state = THREAD_READY;
    thread->kernel_stack_top = 0;
    thread->user_stack_top = USER_STACK_TOP;
    thread->entry_point = USER_CODE_BASE;

    thread->context.rsp = 0;
    thread->context.rbx = 0;
    thread->context.rbp = 0;
    thread->context.r12 = 0;
    thread->context.r13 = 0;
    thread->context.r14 = 0;
    thread->context.r15 = 0;
}