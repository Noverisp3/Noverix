#include "task.h"
#include "memory/pfa.h"
#include "memory/paging.h"
#include "cpu/cpu.h"
#include "cpu/timer.h"
#include "lib.h"

task_t *current_task;

static task_t *ready_head;
static unsigned int next_pid;

static task_t *alloc_task(void)
{
    task_t *t = (task_t *)alloc_frame();
    if (!t) return 0;
    lib_memset(t, 0, FRAME_SIZE);
    t->pid = next_pid++;
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

    /* Add to circular ready list */
    if (!ready_head) {
        ready_head = t;
        t->next = t;
    } else {
        t->next = ready_head->next;
        ready_head->next = t;
    }

    return t;
}

static task_t *pick_next(void)
{
    if (!current_task) return 0;
    task_t *start = current_task;
    task_t *t = current_task->next;

    while (t && t != start) {
        if (t->state == TASK_READY)
            return t;
        t = t->next;
    }
    if (current_task->state == TASK_READY || current_task->state == TASK_RUNNING)
        return current_task;
    return 0;
}

unsigned int task_switch_tick(unsigned int current_esp)
{
    if (!current_task) return 0;
    if (!task_switch_pending) return 0;
    task_switch_pending = 0;

    current_task->state = TASK_READY;
    current_task->kernel_esp = current_esp;

    task_t *next = pick_next();
    if (!next || next == current_task) {
        current_task->state = TASK_RUNNING;
        return 0;
    }

    current_task = next;
    current_task->state = TASK_RUNNING;

    if (current_task->page_dir != kernel_page_dir)
        page_dir_switch(current_task->page_dir);

    return current_task->kernel_esp;
}

void task_init(void)
{
    current_task = 0;
    ready_head = 0;
    next_pid = 1;
}
