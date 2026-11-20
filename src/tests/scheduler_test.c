#include <stdint.h>
#include <str.h>
#include <string.h>
#include <cpu.h>
#include <terminal.h>
#include <thread.h>
#include <kthread.h>
#include <scheduler.h>
#include <kernel.h>

#include "scheduler_test.h"

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

    worker_a.state = THREAD_TERMINATED;

    for (;;) {
        scheduler_yield();
    }
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

    worker_b.state = THREAD_TERMINATED;

    for (;;) {
        scheduler_yield();
    }
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
}