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

static unsigned int lapic_read(unsigned int reg)
{
    return *(lapic + reg / 4);
}

static void lapic_write(unsigned int reg, unsigned int val)
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

    /* Mask all LVT entries */
    lapic_write(LAPIC_LVT_TIMER, 0x00010000);   /* masked */
    lapic_write(LAPIC_LVT_LINT0, 0x00010000);   /* masked */
    lapic_write(LAPIC_LVT_LINT1, 0x00010000);   /* masked */
    lapic_write(LAPIC_LVT_ERROR, 0x00010000);   /* masked */

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
