#ifndef TASK_H
#define TASK_H

#include "memory/paging.h"
#include "sync/sync.h"

#define TASK_FREE    0
#define TASK_READY   1
#define TASK_RUNNING 2
#define TASK_BLOCKED 3
#define TASK_ZOMBIE  4

#define TASK_STACK_SIZE 0x1000

typedef struct task {
    unsigned int        pid;
    volatile int        state;
    int                 cpu_assigned;    /* -1 = free, 0..MAX_CPU = assigned */
    page_dir_t          page_dir;
    unsigned int        kernel_esp;
    void               *kernel_stack_base;
    struct task        *next;
} task_t;

/* Global scheduler lock — protects ready list + cpu_assigned changes */
extern spinlock_t sched_lock;

/* Ready list head + allocator (used by elf.c for user task creation) */
extern task_t *ready_head;
task_t *alloc_task(void);

void task_init(void);
task_t *task_create(void (*entry)(void));
unsigned int task_switch_tick(unsigned int current_esp);
void task_idle_loop(void);

#endif
