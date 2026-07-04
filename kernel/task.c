#include "task.h"
#include "memory/pfa.h"
#include "memory/paging.h"
#include "cpu/cpu.h"
#include "cpu/timer.h"
#include "scheduler/scheduler.h"
#include "lib.h"

spinlock_t sched_lock;

static task_t *ready_head;
static unsigned int next_pid;

static task_t *alloc_task(void)
{
    task_t *t = (task_t *)alloc_frame();
    if (!t) return 0;
    lib_memset(t, 0, FRAME_SIZE);
    t->pid = next_pid++;
    t->cpu_assigned = -1;
    return t;
}

task_t *task_create(void (*entry)(void))
{
    task_t *t = alloc_task();
    if (!t) return 0;

    void *stack = alloc_frames(TASK_STACK_SIZE >> 12);
    if (!stack) {
        free_frame(t);
        return 0;
    }
    t->kernel_stack_base = stack;

    unsigned int *sp = (unsigned int *)((unsigned int)stack + TASK_STACK_SIZE);

    /* iret frame: EFLAGS, CS, EIP */
    *--sp = 0x202;                      /* EFLAGS: IF=1 */
    *--sp = 0x08;                       /* CS: kernel code */
    *--sp = (unsigned int)entry;        /* EIP */

    /* int_no, err_code (popped by addl $8) */
    *--sp = 0;
    *--sp = 0;

    /* pusha: EDI, ESI, EBP, ESP_old, EBX, EDX, ECX, EAX */
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;

    /* segments: DS, ES, FS, GS */
    *--sp = 0x10; *--sp = 0x10; *--sp = 0x10; *--sp = 0x10;

    t->kernel_esp = (unsigned int)sp;

    t->page_dir = kernel_page_dir;
    t->state = TASK_READY;

    /* Add to circular ready list (locked) */
    spinlock_lock(&sched_lock);
    if (!ready_head) {
        ready_head = t;
        t->next = t;
    } else {
        t->next = ready_head->next;
        ready_head->next = t;
    }
    spinlock_unlock(&sched_lock);

    return t;
}

/*
 * Pick the next READY task. Called with sched_lock held.
 * Scans the circular list, skips tasks that are RUNNING or
 * already assigned to another CPU (cpu_assigned >= 0).
 */
static task_t *pick_next_locked(void)
{
    int cpu = get_cpu_id();
    task_t *curr = cpu_info[cpu].current_task;
    if (!curr || !ready_head) return 0;

    task_t *start = curr;
    task_t *t = curr->next;

    while (t != start) {
        if (t->state == TASK_READY && t->cpu_assigned < 0 &&
            t->kernel_esp != 0)
            return t;
        t = t->next;
    }

    /* Check current task as last resort */
    if (start->state == TASK_READY && start->cpu_assigned < 0 &&
        start->kernel_esp != 0)
        return start;

    return 0;
}

unsigned int task_switch_tick(unsigned int current_esp)
{
    int cpu = get_cpu_id();
    task_t *curr = cpu_info[cpu].current_task;

    if (!curr) return 0;
    if (!task_switch_pending) return 0;
    task_switch_pending = 0;

    /* Save current context */
    curr->kernel_esp = current_esp;

    spinlock_lock(&sched_lock);

    /* Release current task for re-scheduling */
    curr->state = TASK_READY;
    curr->cpu_assigned = -1;

    /* Find next task */
    task_t *next = pick_next_locked();

    if (!next) {
        /* No runnable task — keep current running */
        curr->state = TASK_RUNNING;
        curr->cpu_assigned = cpu;
        spinlock_unlock(&sched_lock);
        return 0;
    }

    if (next == curr) {
        /* Same task, no switch needed */
        curr->state = TASK_RUNNING;
        curr->cpu_assigned = cpu;
        spinlock_unlock(&sched_lock);
        return 0;
    }

    /* Commit to new task */
    next->state = TASK_RUNNING;
    next->cpu_assigned = cpu;
    cpu_info[cpu].current_task = next;
    spinlock_unlock(&sched_lock);

    if (next->page_dir != kernel_page_dir)
        page_dir_switch(next->page_dir);

    return next->kernel_esp;
}

void task_idle_loop(void)
{
    for (;;) {
        if (!scheduler_step())
            __asm__ volatile ("pause");
    }
}

void task_init(void)
{
    int cpu = get_cpu_id();

    spinlock_init(&sched_lock);
    ready_head = 0;
    next_pid = 1;
    cpu_info[cpu].current_task = 0;
}
