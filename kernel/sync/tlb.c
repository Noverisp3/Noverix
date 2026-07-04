#include "tlb.h"
#include "../apic/lapic.h"
#include "../drivers/serial.h"

void tlb_init(void)
{
    register_interrupt_handler(TLB_IPI_VECTOR, tlb_ipi_handler);
    serial_write_string("[tlb] init (IPI vector 0x50)\n");
}

void tlb_shootdown(unsigned int virt)
{
    /* Flush local TLB */
    __asm__ volatile ("invlpg (%0)" : : "r" (virt) : "memory");

    /* Send TLB flush IPI to all other CPUs */
    if (apic_enabled)
        lapic_send_ipi_all_exc_self(TLB_IPI_VECTOR);
}

/* Called on remote CPUs via IPI — must NOT acquire any locks */
void tlb_ipi_handler(registers_t *regs)
{
    (void)regs;

    /* LAPIC EOI (received as fixed IPI through LAPIC) */
    lapic_eoi();

    /* Full TLB flush via CR3 reload */
    __asm__ volatile (
        "mov %%cr3, %%eax\n"
        "mov %%eax, %%cr3\n"
        : : : "eax", "memory"
    );
}
