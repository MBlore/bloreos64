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
#ifndef _BLOREOS_THREAD_H
#define _BLOREOS_THREAD_H

#include <stdint.h>
#include <process.h>

#define THREAD_KERNEL_STACK_SIZE           (16 * 1024)
#define THREAD_KERNEL_EXEC_STACK_SIZE       (8 * 1024)
#define THREAD_KERNEL_INTERRUPT_STACK_SIZE  (8 * 1024)

typedef uint64_t tid_t;

typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_TERMINATED
} thread_state_t;

// thread_context saves registers for switching between scheduled kernel threads.
typedef struct thread_context {
    uint64_t rsp;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
} thread_context_t;

// usermode_context saves the kernel return information when entering Ring 3.
typedef struct usermode_context {
    uint64_t kernel_rsp;
    uint64_t kernel_return;
    uint64_t kernel_cr3;
} usermode_context_t;

typedef struct thread {
    tid_t tid;
    process_t *process;
    thread_state_t state;

    thread_context_t context;
    usermode_context_t usermode;

    void *kernel_stack;
    uint64_t kernel_stack_top;
    uint64_t user_stack_top;
    uint64_t entry_point;
} thread_t;

void context_switch(thread_context_t *previous, thread_context_t *next);

thread_t    *thread_create(process_t *process);
void        thread_destroy(thread_t *thread);

#endif