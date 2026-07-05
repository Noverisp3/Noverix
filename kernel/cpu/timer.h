#ifndef TIMER_H
#define TIMER_H

void init_timer(unsigned int freq);
unsigned int get_ticks(void);
void sleep_ms(unsigned int ms);

#endif
