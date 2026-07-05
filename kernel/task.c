#include "task.h"
#include "memory/pfa.h"
#include "memory/paging.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/timer.h"
#include "scheduler/scheduler.h"
#include "drivers/serial.h"
#include "lib.h"

spinlock_t sched_lock;

task_t *ready_head;
static unsigned int next_pid;

task_t *alloc_task(void)
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

    /* segments: DS, ES, FS, GS (GS = GDT_PERCPU for per-CPU data) */
    *--sp = 0x10; *--sp = 0x10; *--sp = 0x10; *--sp = 0x30;

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

/*
 * Internal: release current task, pick next, return its kernel_esp.
 * If new_state != TASK_READY, the current task is set to new_state
 * instead of being released for re-scheduling (used for blocking).
 */
static unsigned int task_switch_from(unsigned int current_esp,
                                     int new_state,
                                     unsigned int wakeup_tick)
{
    int cpu = get_cpu_id();
    task_t *curr = cpu_info[cpu].current_task;

    if (!curr) return 0;

    curr->kernel_esp = current_esp;
    curr->wakeup_tick = wakeup_tick;

    spinlock_lock(&sched_lock);

    if (new_state == TASK_READY) {
        curr->state = TASK_READY;
        curr->cpu_assigned = -1;
    } else {
        curr->state = new_state;
        curr->cpu_assigned = -1;
    }

    task_t *next = pick_next_locked();

    if (!next) {
        /* No runnable task — re-assign current */
        curr->state = TASK_RUNNING;
        curr->cpu_assigned = cpu;
        spinlock_unlock(&sched_lock);
        return 0;
    }

    if (next == curr) {
        curr->state = TASK_RUNNING;
        curr->cpu_assigned = cpu;
        spinlock_unlock(&sched_lock);
        return 0;
    }

    next->state = TASK_RUNNING;
    next->cpu_assigned = cpu;
    cpu_info[cpu].current_task = next;
    spinlock_unlock(&sched_lock);

    gdt_set_kernel_stack(cpu, (unsigned int)next->kernel_stack_base + TASK_STACK_SIZE);
    page_dir_switch(next->page_dir);

    return next->kernel_esp;
}

unsigned int task_block_and_switch(unsigned int current_esp,
                                   unsigned int wakeup_tick)
{
    return task_switch_from(current_esp, TASK_BLOCKED, wakeup_tick);
}

unsigned int task_yield(unsigned int current_esp)
{
    return task_switch_from(current_esp, TASK_READY, 0);
}

unsigned int task_switch_tick(unsigned int current_esp)
{
    int cpu = get_cpu_id();
    task_t *curr = cpu_info[cpu].current_task;

    if (!curr) return 0;
    if (!cpu_info[cpu].resched_pending) return 0;
    cpu_info[cpu].resched_pending = 0;

    return task_switch_from(current_esp, TASK_READY, 0);
}

void task_idle_loop(void)
{
    for (;;) {
        if (!scheduler_step())
            __asm__ volatile ("pause");
    }
}

void task_foreach(task_callback_t cb, void *arg)
{
    if (!ready_head) return;
    unsigned int flags = spinlock_lock_irqsave(&sched_lock);
    task_t *start = ready_head;
    task_t *t = start;
    do {
        task_t *next = t->next;
        cb(t, arg);
        t = next;
    } while (t != start);
    spinlock_unlock_irqrestore(&sched_lock, flags);
}

int task_kill(unsigned int pid)
{
    if (!ready_head) return -1;
    unsigned int flags = spinlock_lock_irqsave(&sched_lock);
    task_t *t = ready_head;
    do {
        if (t->pid == pid) {
            if (t->state == TASK_RUNNING) {
                spinlock_unlock_irqrestore(&sched_lock, flags);
                return -2;
            }
            /* Remove from circular list */
            if (t->next == t) {
                ready_head = 0;
            } else {
                /* Find previous node */
                task_t *p = t;
                while (p->next != t)
                    p = p->next;
                p->next = t->next;
                if (ready_head == t)
                    ready_head = t->next;
            }
            t->state = TASK_FREE;
            t->cpu_assigned = -1;
            /* Free user resources (page dir, kernel stack) */
            if (t->page_dir && t->page_dir != kernel_page_dir) {
                for (int i = 0; i < 1024; i++) {
                    if (t->page_dir[i] == kernel_page_dir[i])
                        continue;
                    if (t->page_dir[i] & PAGE_PRESENT) {
                        page_table_entry_t *pt = (page_table_entry_t *)(t->page_dir[i] & 0xFFFFF000);
                        for (int j = 0; j < 1024; j++) {
                            if (pt[j] & PAGE_PRESENT)
                                free_frame((void *)(pt[j] & 0xFFFFF000));
                        }
                        free_frame((void *)pt);
                    }
                }
                free_frame((void *)t->page_dir);
                t->page_dir = 0;
            }
            if (t->kernel_stack_base) {
                free_frames(t->kernel_stack_base, TASK_STACK_SIZE >> 12);
                t->kernel_stack_base = 0;
            }
            spinlock_unlock_irqrestore(&sched_lock, flags);
            return 0;
        }
        t = t->next;
    } while (t != ready_head);
    spinlock_unlock_irqrestore(&sched_lock, flags);
    return -1;
}

void debug_ctx_switch(unsigned int kernel_esp, unsigned int frame_eip)
{
    serial_write_string("[ctx] esp=");
    serial_write_hex(kernel_esp);
    serial_write_string(" eip=");
    serial_write_hex(frame_eip);
    serial_write_string("\n");
}

void task_init(void)
{
    int cpu = get_cpu_id();
    spinlock_init(&sched_lock);
    next_pid = 1;
    cpu_info[cpu].current_task = 0;
    cpu_info[cpu].resched_pending = 0;
}
