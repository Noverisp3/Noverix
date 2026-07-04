#ifndef CPU_H
#define CPU_H

#define MAX_CPU 8

typedef enum {
    CPU_UNINITIALIZED = 0,
    CPU_RUNNING,
    CPU_IDLE,
    CPU_HALTED
} cpu_state_t;

typedef struct {
    int cpu_id;
    unsigned int apic_id;
    cpu_state_t state;
    void *stack_top;
} cpu_info_t;

extern int cpu_count;
extern cpu_info_t cpu_info[MAX_CPU];

/* Returns current CPU ID via %gs segment */
static inline int get_cpu_id(void)
{
    int id;
    __asm__ ("movl %%gs:0, %0" : "=r" (id));
    return id;
}

#endif
