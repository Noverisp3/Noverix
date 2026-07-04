#ifndef TIMER_H
#define TIMER_H

extern volatile int task_switch_pending;

void init_timer(unsigned int freq);
unsigned int get_ticks(void);
void sleep_ms(unsigned int ms);

#endif
