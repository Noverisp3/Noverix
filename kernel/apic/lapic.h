#ifndef LAPIC_H
#define LAPIC_H

#include "../cpu/idt.h"

#define LAPIC_BASE_PHYS 0xFEE00000
#define MSR_APIC_BASE   0x1B

/* LAPIC register offsets */
#define LAPIC_ID        0x020
#define LAPIC_VERSION   0x030
#define LAPIC_TPR       0x080
#define LAPIC_EOI       0x0B0
#define LAPIC_SVR       0x0F0
#define LAPIC_ICR_LOW   0x300
#define LAPIC_ICR_HIGH  0x310
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370

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
