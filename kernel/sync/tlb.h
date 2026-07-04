#ifndef TLB_H
#define TLB_H

#include "../cpu/idt.h"

#define TLB_IPI_VECTOR 0x50

void tlb_init(void);
void tlb_shootdown(unsigned int virt);
void tlb_ipi_handler(registers_t *regs);

#endif