#include "lapic.h"
#include "../cpu/ports.h"
#include "../drivers/serial.h"
#include "../memory/paging.h"
#include "../acpi/acpi.h"

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

static volatile unsigned int *lapic;
int apic_enabled;

unsigned int lapic_read(unsigned int reg)
{
    return *(lapic + reg / 4);
}

void lapic_write(unsigned int reg, unsigned int val)
{
    *(lapic + reg / 4) = val;
}

void lapic_init(void)
{
    unsigned int phys = acpi_get_lapic_addr();
    if (!phys)
        phys = LAPIC_BASE_PHYS;

    serial_write_string("[apic] mapping LAPIC at ");
    serial_write_hex(phys);
    serial_write_char('\n');

    /* Identity-map LAPIC physical page */
    map_page(phys, phys, PAGE_WRITE);
    lapic = (volatile unsigned int *)phys;

    /* Enable LAPIC via MSR */
    unsigned int low, high;
    __asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (MSR_APIC_BASE));
    serial_write_string("[apic] APIC_BASE MSR=");
    serial_write_hex(low);
    serial_write_char('\n');

    if (!(low & (1 << 11))) {
        low |= (1 << 11);   /* enable APIC */
        __asm__ volatile ("wrmsr" : : "a" (low), "d" (high), "c" (MSR_APIC_BASE));
        serial_write_string("[apic] LAPIC enabled via MSR\n");
    }

    /* Spurious vector register: enable APIC + vector 0xFF */
    lapic_write(LAPIC_SVR, (1 << 8) | 0xFF);

    /* Task priority = 0 (allow all interrupts) */
    lapic_write(LAPIC_TPR, 0);

    /* Mask LVT timer and error */
    lapic_write(LAPIC_LVT_TIMER, 0x00010000);   /* masked */
    lapic_write(LAPIC_LVT_ERROR, 0x00010000);   /* masked */

    /* LINT0 = ExtINT mode (virtual wire: PIC interrupts delivered through LAPIC) */
    lapic_write(LAPIC_LVT_LINT0, 0x00000700);   /* ExtINT, unmasked */

    /* LINT1 = NMI */
    lapic_write(LAPIC_LVT_LINT1, 0x00000400);   /* NMI, unmasked */

    /* Read APIC ID to verify */
    unsigned int id = lapic_read(LAPIC_ID);
    serial_write_string("[apic] LAPIC ID=");
    serial_write_hex(id >> 24);
    serial_write_string(" version=");
    serial_write_hex(lapic_read(LAPIC_VERSION));
    serial_write_char('\n');

    apic_enabled = 1;
}

void lapic_eoi(void)
{
    if (apic_enabled)
        lapic_write(LAPIC_EOI, 0);
}

/* Called for spurious interrupts (vector 0xFF) — must NOT send EOI */
void spurious_handler(registers_t *regs)
{
    (void)regs;
}

void lapic_send_ipi(unsigned int apic_id, unsigned int icr_low)
{
    lapic_write(LAPIC_ICR_HIGH, apic_id << 24);
    lapic_write(LAPIC_ICR_LOW, icr_low);
}

/* Send IPI to all CPUs except self (dest shorthand = 11) */
void lapic_send_ipi_all_exc_self(unsigned int vector)
{
    lapic_write(LAPIC_ICR_HIGH, 0);
    lapic_write(LAPIC_ICR_LOW, 0x000C0000 | vector);
}

void lapic_start_ap(unsigned int apic_id, unsigned int trampoline_page)
{
    /* Disable interrupts — the INIT IPI blocks LAPIC delivery of timer IRQs
       and may interfere with ICR delivery on some emulated platforms. */
    __asm__ volatile ("cli");

    /* INIT level assert */
    lapic_send_ipi(apic_id, 0x0000C500);
    for (volatile int i = 0; i < 5000000; i++);

    /* INIT de-assert */
    lapic_send_ipi(apic_id, 0x00008500);
    for (volatile int i = 0; i < 5000000; i++);

    /* First SIPI */
    lapic_send_ipi(apic_id, 0x00000600 | trampoline_page);
    for (volatile int i = 0; i < 100000; i++);

    /* Second SIPI */
    lapic_send_ipi(apic_id, 0x00000600 | trampoline_page);

    __asm__ volatile ("sti");
}
