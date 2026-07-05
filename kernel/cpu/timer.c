#include "timer.h"
#include "idt.h"
#include "ports.h"
#include "cpu.h"
#include "../task.h"
#include "../apic/lapic.h"

#define PIT_CONTROL 0x43
#define PIT_CHANNEL0 0x40
#define PIT_FREQ 1193182

static volatile unsigned int tick_count;
static unsigned int tick_ms;

/* AP reschedule IPI handler — sets resched_pending on the receiving CPU */
static void resched_ipi_handler(registers_t *regs)
{
    (void)regs;
    int cpu = get_cpu_id();
    cpu_info[cpu].resched_pending = 1;
}

static void timer_handler(registers_t *regs)
{
    (void)regs;
    int cpu = get_cpu_id();

    /* Only BSP increments tick_count to avoid double-counting */
    if (cpu == 0)
        tick_count++;

    /* Wake BLOCKED tasks whose wakeup_tick has elapsed */
    if (cpu == 0 && ready_head) {
        spinlock_lock(&sched_lock);
        task_t *start = ready_head;
        task_t *t = start;
        do {
            if (t->state == TASK_BLOCKED && t->wakeup_tick &&
                tick_count >= t->wakeup_tick) {
                t->state = TASK_READY;
                t->wakeup_tick = 0;
            }
            t = t->next;
        } while (t != start);
        spinlock_unlock(&sched_lock);
    }

    cpu_info[cpu].resched_pending = 1;

    /* Notify APs that rescheduling is needed */
    if (cpu == 0 && cpu_count > 1)
        lapic_send_ipi_all_exc_self(0x51);
}

void init_timer(unsigned int freq)
{
    tick_count = 0;
    tick_ms = 1000 / freq;

    unsigned int divisor = PIT_FREQ / freq;
    outb(PIT_CONTROL, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    register_interrupt_handler(32, timer_handler);
    register_interrupt_handler(0x51, resched_ipi_handler);
}

unsigned int get_ticks(void)
{
    return tick_count;
}

void sleep_ms(unsigned int ms)
{
    unsigned int start = tick_count;
    unsigned int ticks_to_wait = (ms + tick_ms - 1) / tick_ms;
    while ((tick_count - start) < ticks_to_wait);
}
