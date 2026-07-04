#include "ioapic.h"
#include "../cpu/ports.h"
#include "../drivers/serial.h"
#include "../memory/paging.h"
#include "../acpi/acpi.h"

#define IOAPIC_BASE_PHYS 0xFEC00000

static volatile unsigned int *ioapic;

static unsigned int ioapic_read(unsigned int reg)
{
    *(volatile unsigned int *)(ioapic + 0x00 / 4) = reg;
    return *(volatile unsigned int *)(ioapic + 0x10 / 4);
}

static void ioapic_write(unsigned int reg, unsigned int val)
{
    *(volatile unsigned int *)(ioapic + 0x00 / 4) = reg;
    *(volatile unsigned int *)(ioapic + 0x10 / 4) = val;
}

static void ioapic_set_irq(int irq, unsigned int apic_id, unsigned int vector, int masked)
{
    unsigned int low = vector;
    if (masked)
        low |= (1 << 16);
    unsigned int high = (apic_id << 24);

    ioapic_write(0x10 + 2 * irq, low);
    ioapic_write(0x10 + 2 * irq + 1, high);
}

void ioapic_init(void)
{
    unsigned int phys = IOAPIC_BASE_PHYS;

    serial_write_string("[apic] mapping I/O APIC at ");
    serial_write_hex(phys);
    serial_write_char('\n');

    map_page(phys, phys, PAGE_WRITE);
    ioapic = (volatile unsigned int *)phys;

    unsigned int ver = ioapic_read(0x01);
    unsigned int max_entries = (ver >> 16) & 0xFF;
    serial_write_string("[apic] I/O APIC version=");
    serial_write_hex(ver & 0xFF);
    serial_write_string(" max_entries=");
    serial_write_int(max_entries + 1);
    serial_write_char('\n');

    /* Route IRQ 0 (PIT) → vector 0x20, unmasked, dest = BSP (APIC ID 0) */
    ioapic_set_irq(0, 0, 0x20, 0);

    /* Route IRQ 1 (keyboard) → vector 0x21, unmasked, dest = BSP */
    ioapic_set_irq(1, 0, 0x21, 0);

    /* Mask all other IRQs */
    for (unsigned int i = 2; i <= max_entries; i++)
        ioapic_set_irq(i, 0, 0, 1);

    /* Program IMCR to route interrupts through APIC (not PIC) */
    outb(0x22, 0x70);
    outb(0x23, 1);

    serial_write_string("[apic] I/O APIC OK\n");
}
