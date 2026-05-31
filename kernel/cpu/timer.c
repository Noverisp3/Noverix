#include "timer.h"
#include "idt.h"
#include "ports.h"

#define PIT_CONTROL 0x43
#define PIT_CHANNEL0 0x40
#define PIT_FREQ 1193182

static volatile unsigned int tick_count;
static unsigned int tick_ms;

static __attribute__((always_inline)) inline unsigned int timer_get_tick()
{
    unsigned int tick;
    __asm__ volatile("mov %%eax, %%ecx;"
                     "lock xaddl %%ecx, %0;"
                     "mov %%ecx, %%eax;"
                     : "=m" (tick_count), "=a" (tick)
                     : "m" (tick_count)
                     : "ecx", "memory");
    return tick;
}

static void timer_handler(registers_t *regs)
{
    (void)regs;
    (void)timer_get_tick();
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