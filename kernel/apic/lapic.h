#ifndef LAPIC_H
#define LAPIC_H

#include "../cpu/idt.h"

void lapic_init(void);
void lapic_eoi(void);
void spurious_handler(registers_t *regs);

extern int apic_enabled;

#endif
