#include "scheduler.h"
#include "../sync/sync.h"
#include "../drivers/serial.h"
#include "../lib.h"
#include "../cpu/cpu.h"

#define MAX_TASKS 64

typedef struct {
    task_fn fn;
    void *arg;
    volatile int state; /* 0=free, 1=submitted, 2=running */
    volatile int cpu_id;
} task_entry_t;

static task_entry_t tasks[MAX_TASKS];
static spinlock_t sched_lock = SPINLOCK_INIT;
static volatile int next_id = 1;

void scheduler_init(void)
{
    lib_memset((void *)tasks, 0, sizeof(tasks));
    next_id = 1;
    serial_write_string("[sched] init\n");
}

int scheduler_submit(task_fn fn, void *arg)
{
    unsigned int flags = spinlock_lock_irqsave(&sched_lock);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == 0) {
            tasks[i].fn = fn;
            tasks[i].arg = arg;
            tasks[i].state = 1;
            tasks[i].cpu_id = -1;
            int id = next_id++;
            spinlock_unlock_irqrestore(&sched_lock, flags);
            return id;
        }
    }
    spinlock_unlock_irqrestore(&sched_lock, flags);
    return 0;
}

void scheduler_wait_all(void)
{
    for (;;) {
        unsigned int flags = spinlock_lock_irqsave(&sched_lock);
        int pending = 0;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (tasks[i].state == 1 || tasks[i].state == 2) {
                pending = 1;
                break;
            }
        }
        spinlock_unlock_irqrestore(&sched_lock, flags);
        if (!pending) break;

        /* Help process tasks ourselves instead of just waiting */
        scheduler_step();
    }
}

int scheduler_pending(void)
{
    unsigned int flags = spinlock_lock_irqsave(&sched_lock);
    int count = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == 1)
            count++;
    }
    spinlock_unlock_irqrestore(&sched_lock, flags);
    return count;
}

int scheduler_step(void)
{
    unsigned int flags = spinlock_lock_irqsave(&sched_lock);
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == 1) {
            task_fn fn = tasks[i].fn;
            void *arg = tasks[i].arg;
            tasks[i].state = 2;
            spinlock_unlock_irqrestore(&sched_lock, flags);
            fn(arg);
            flags = spinlock_lock_irqsave(&sched_lock);
            tasks[i].state = 0;
            tasks[i].cpu_id = get_cpu_id();
            spinlock_unlock_irqrestore(&sched_lock, flags);
            return 1;
        }
    }
    spinlock_unlock_irqrestore(&sched_lock, flags);
    return 0;
}
