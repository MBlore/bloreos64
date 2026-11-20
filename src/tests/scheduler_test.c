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
#include <str.h>
#include <string.h>
#include <cpu.h>
#include <terminal.h>
#include <thread.h>
#include <kthread.h>
#include <scheduler.h>
#include <kernel.h>

#include "tests.h"

static thread_t worker_a;
static thread_t worker_b;

static uint8_t worker_a_stack[KTHREAD_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t worker_b_stack[KTHREAD_STACK_SIZE] __attribute__((aligned(16)));

/*
 * Runs the first cooperative scheduling test worker.
 */
static void worker_a_entry(void)
{
    for (uint32_t i = 1; i <= 3; i++) {
        klogf("Thread A: iteration %u\n", i);
        scheduler_yield();
    }

    scheduler_exit();
}

/*
 * Runs the second cooperative scheduling test worker.
 */
static void worker_b_entry(void)
{
    for (uint32_t i = 1; i <= 3; i++) {
        klogf("Thread B: iteration %u\n", i);
        scheduler_yield();
    }

    scheduler_exit();
}

/*
 * Verifies that terminated thread registrations can be reclaimed
 * and their scheduler slots reused.
 */
static void scheduler_test_reuse(void)
{
    worker_a.state = THREAD_READY;

    if (!kthread_prepare(&worker_a, worker_a_entry, worker_a_stack, sizeof(worker_a_stack))) {
        klogf("Scheduler: worker reuse preparation failed\n");
        hcf();
    }

    if (!scheduler_add(&worker_a)) {
        klogf("Scheduler: worker reuse registration failed\n");
        hcf();
    }

    klogf("Scheduler: restarting thread A\n");

    while (worker_a.state != THREAD_TERMINATED) {
        scheduler_yield();
    }

    uint32_t reaped = scheduler_reap();
    klogf("Scheduler: reclaimed %u reused thread\n", reaped);
}

/*
 * Runs the cooperative round-robin scheduler integration test.
 *
 * Assumes the scheduler has already been initialised and the bootstrap
 * kernel thread is registered as the currently running thread.
 */
void scheduler_test_run(void)
{
    memset(&worker_a, 0, sizeof(worker_a));
    memset(&worker_b, 0, sizeof(worker_b));

    worker_a.tid = 2;
    worker_b.tid = 3;

    worker_a.state = THREAD_READY;
    worker_b.state = THREAD_READY;

    if (!kthread_prepare(&worker_a, worker_a_entry, worker_a_stack, sizeof(worker_a_stack))) {
        klogf("Failed to prepare thread A\n");
        hcf();
    }

    if (!kthread_prepare(&worker_b, worker_b_entry, worker_b_stack, sizeof(worker_b_stack))) {
        klogf("Failed to prepare thread B\n");
        hcf();
    }

    if (!scheduler_add(&worker_a) || !scheduler_add(&worker_b)) {
        klogf("Failed to register kernel workers\n");
        hcf();
    }

    klogf("Scheduler: starting workers\n");

    while (worker_a.state != THREAD_TERMINATED || worker_b.state != THREAD_TERMINATED) {
        scheduler_yield();
    }

    klogf("Scheduler: both workers terminated\n");

    uint32_t reaped = scheduler_reap();

    klogf("Scheduler: reaped %u terminated threads\n", reaped);

    scheduler_test_reuse();
}
