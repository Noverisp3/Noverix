#ifndef LAPIC_H
#define LAPIC_H

#include "../cpu/idt.h"

void lapic_init(void);
void lapic_eoi(void);
void spurious_handler(registers_t *regs);

unsigned int lapic_read(unsigned int reg);
void lapic_write(unsigned int reg, unsigned int val);
void lapic_send_ipi(unsigned int apic_id, unsigned int icr_low);
void lapic_send_ipi_all_exc_self(unsigned int vector);
void lapic_start_ap(unsigned int apic_id, unsigned int trampoline_page);

extern int apic_enabled;

#endif
