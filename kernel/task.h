#ifndef TASK_H
#define TASK_H

#include "memory/paging.h"

#define TASK_FREE    0
#define TASK_READY   1
#define TASK_RUNNING 2
#define TASK_BLOCKED 3
#define TASK_ZOMBIE  4

#define TASK_STACK_SIZE 0x1000

typedef struct task {
    unsigned int        pid;
    volatile int        state;
    page_dir_t          page_dir;
    unsigned int        kernel_esp;
    void               *kernel_stack_base;
    struct task        *next;
} task_t;

extern task_t *current_task;

void task_init(void);
task_t *task_create(void (*entry)(void));
unsigned int task_switch_tick(unsigned int current_esp);

#endif
