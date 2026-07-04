#ifndef SCHEDULER_H
#define SCHEDULER_H

typedef void (*task_fn)(void *arg);

void scheduler_init(void);
int  scheduler_submit(task_fn fn, void *arg);
void scheduler_wait_all(void);
int  scheduler_pending(void);

/* Called by AP idle loop — picks and runs one task if available.
 * Returns 1 if a task was run, 0 if queue was empty. */
int  scheduler_step(void);

#endif
