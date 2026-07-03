#include "idt.h"
#include "ports.h"
#include "../drivers/screen.h"
#include "../drivers/serial.h"

typedef struct {
    unsigned short base_low;
    unsigned short sel;
    unsigned char always0;
    unsigned char flags;
    unsigned short base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt_entries[256];
static idt_ptr_t idt_ptr;
static interrupt_handler_t interrupt_handlers[256];

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

static void idt_set_entry(unsigned char num, unsigned int base, unsigned short sel, unsigned char flags)
{
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].base_high = (base >> 16) & 0xFFFF;
    idt_entries[num].sel = sel;
    idt_entries[num].always0 = 0;
    idt_entries[num].flags = flags;
}

static const char *exception_names[32] = {
    "Division By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Security Exception",
    "Reserved"
};

static unsigned int read_cr0(void)
{
    unsigned int val;
    __asm__ volatile ("mov %%cr0, %0" : "=r" (val));
    return val;
}

static unsigned int read_cr2(void)
{
    unsigned int val;
    __asm__ volatile ("mov %%cr2, %0" : "=r" (val));
    return val;
}

static unsigned int read_cr3(void)
{
    unsigned int val;
    __asm__ volatile ("mov %%cr3, %0" : "=r" (val));
    return val;
}

static void dump_registers(registers_t *regs)
{
    print_string("EIP: "); print_hex(regs->eip);
    print_string("  CS: "); print_hex(regs->cs);
    print_string("  EFLAGS: "); print_hex(regs->eflags);
    print_string("\n\n");

    print_string("EAX: "); print_hex(regs->eax);
    print_string("  EBX: "); print_hex(regs->ebx);
    print_string("  ECX: "); print_hex(regs->ecx);
    print_string("  EDX: "); print_hex(regs->edx);
    print_string("\n");

    print_string("ESI: "); print_hex(regs->esi);
    print_string("  EDI: "); print_hex(regs->edi);
    print_string("  EBP: "); print_hex(regs->ebp);
    print_string("  ESP: "); print_hex(regs->esp);
    print_string("\n\n");

    print_string("DS: "); print_hex(regs->ds);
    print_string("  ES: "); print_hex(regs->es);
    print_string("  FS: "); print_hex(regs->fs);
    print_string("  GS: "); print_hex(regs->gs);
    print_string("\n");

    print_string("CR0: "); print_hex(read_cr0());
    print_string("  CR2: "); print_hex(read_cr2());
    print_string("  CR3: "); print_hex(read_cr3());
    print_string("\n");
}

static void dump_pagefault(registers_t *regs)
{
    unsigned int cr2 = read_cr2();
    print_string("CR2 (fault addr): "); print_hex(cr2); print_string("\n");

    unsigned int ec = regs->err_code;
    print_string("Page fault: ");
    print_string(ec & 1 ? "page-protection" : "not-present");
    print_string(", ");
    print_string(ec & 2 ? "write" : "read");
    print_string(", ");
    print_string(ec & 4 ? "user" : "supervisor");
    print_string("\n");
}

static void dump_backtrace(registers_t *regs)
{
    unsigned int *ebp = (unsigned int *)regs->ebp;
    print_string("Stack trace:\n");
    int depth = 0;
    while (ebp && (unsigned int)ebp >= 0x00010000 && (unsigned int)ebp < 0x00090000 && depth < 16) {
        unsigned int eip = ebp[1];
        print_string("  ["); print_hex(eip); print_string("]\n");
        ebp = (unsigned int *)ebp[0];
        depth++;
    }
}

static void exception_handler(registers_t *regs)
{
    clear_screen();
    print_string("\n!!! CPU EXCEPTION !!!\n\n");
    print_string("Exception: ");
    print_hex(regs->int_no);
    print_string(" - ");
    if (regs->int_no < 32)
        print_string(exception_names[regs->int_no]);
    else
        print_string("Unknown");
    print_string("\n");

    print_string("Error code: "); print_hex(regs->err_code);
    print_string("\n\n");

    if (regs->int_no == 0x0E)
        dump_pagefault(regs);

    dump_registers(regs);

    dump_backtrace(regs);

    print_string("\nSystem halted.\n");

    serial_write_string("\n!!! CPU EXCEPTION !!!\n");
    serial_write_string("Exception: "); serial_write_hex(regs->int_no);
    serial_write_string(" - ");
    if (regs->int_no < 32)
        serial_write_string(exception_names[regs->int_no]);
    serial_write_string("\n");
    serial_write_string("Error code: "); serial_write_hex(regs->err_code);
    serial_write_string(" EIP: "); serial_write_hex(regs->eip);
    serial_write_string(" CS: "); serial_write_hex(regs->cs);
    serial_write_string("\n");
    if (regs->int_no == 0x0E)
        serial_write_hex(read_cr2());
    serial_write_string("\n");

    __asm__ volatile ("cli; hlt");
    while (1);
}

void isr_handler(registers_t *regs)
{
    if (interrupt_handlers[regs->int_no])
        interrupt_handlers[regs->int_no](regs);
    else if (regs->int_no < 32)
        exception_handler(regs);
}

void irq_handler(registers_t *regs)
{
    if (regs->int_no >= 40)
        outb(0xA0, 0x20);
    outb(0x20, 0x20);

    if (interrupt_handlers[regs->int_no])
        interrupt_handlers[regs->int_no](regs);
}

static void irq_remap(void)
{
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFC);   /* master: unmask IRQ0 (timer) + IRQ1 (keyboard) */
    outb(0xA1, 0x00);   /* slave: unmask all IRQs (8-15) */
}

void register_interrupt_handler(int irq, interrupt_handler_t handler)
{
    __asm__ volatile (
        "pushf\n\t"
        "cli\n\t"
        "movl %1, %0\n\t"
        "popf\n\t"
        : "=m" (interrupt_handlers[irq])
        : "r" (handler)
        : "memory"
    );
}

void init_idt(void)
{
    int i;

    idt_ptr.limit = sizeof(idt_entries) - 1;
    idt_ptr.base = (unsigned int)&idt_entries;

    for (i = 0; i < 256; i++)
        idt_set_entry(i, 0, 0, 0);

    idt_set_entry(0, (unsigned int)isr0, 0x08, 0x8E);
    idt_set_entry(1, (unsigned int)isr1, 0x08, 0x8E);
    idt_set_entry(2, (unsigned int)isr2, 0x08, 0x8E);
    idt_set_entry(3, (unsigned int)isr3, 0x08, 0x8E);
    idt_set_entry(4, (unsigned int)isr4, 0x08, 0x8E);
    idt_set_entry(5, (unsigned int)isr5, 0x08, 0x8E);
    idt_set_entry(6, (unsigned int)isr6, 0x08, 0x8E);
    idt_set_entry(7, (unsigned int)isr7, 0x08, 0x8E);
    idt_set_entry(8, (unsigned int)isr8, 0x08, 0x8E);
    idt_set_entry(9, (unsigned int)isr9, 0x08, 0x8E);
    idt_set_entry(10, (unsigned int)isr10, 0x08, 0x8E);
    idt_set_entry(11, (unsigned int)isr11, 0x08, 0x8E);
    idt_set_entry(12, (unsigned int)isr12, 0x08, 0x8E);
    idt_set_entry(13, (unsigned int)isr13, 0x08, 0x8E);
    idt_set_entry(14, (unsigned int)isr14, 0x08, 0x8E);
    idt_set_entry(15, (unsigned int)isr15, 0x08, 0x8E);
    idt_set_entry(16, (unsigned int)isr16, 0x08, 0x8E);
    idt_set_entry(17, (unsigned int)isr17, 0x08, 0x8E);
    idt_set_entry(18, (unsigned int)isr18, 0x08, 0x8E);
    idt_set_entry(19, (unsigned int)isr19, 0x08, 0x8E);
    idt_set_entry(20, (unsigned int)isr20, 0x08, 0x8E);
    idt_set_entry(21, (unsigned int)isr21, 0x08, 0x8E);
    idt_set_entry(22, (unsigned int)isr22, 0x08, 0x8E);
    idt_set_entry(23, (unsigned int)isr23, 0x08, 0x8E);
    idt_set_entry(24, (unsigned int)isr24, 0x08, 0x8E);
    idt_set_entry(25, (unsigned int)isr25, 0x08, 0x8E);
    idt_set_entry(26, (unsigned int)isr26, 0x08, 0x8E);
    idt_set_entry(27, (unsigned int)isr27, 0x08, 0x8E);
    idt_set_entry(28, (unsigned int)isr28, 0x08, 0x8E);
    idt_set_entry(29, (unsigned int)isr29, 0x08, 0x8E);
    idt_set_entry(30, (unsigned int)isr30, 0x08, 0x8E);
    idt_set_entry(31, (unsigned int)isr31, 0x08, 0x8E);

    idt_set_entry(32, (unsigned int)irq0, 0x08, 0x8E);
    idt_set_entry(33, (unsigned int)irq1, 0x08, 0x8E);
    idt_set_entry(34, (unsigned int)irq2, 0x08, 0x8E);
    idt_set_entry(35, (unsigned int)irq3, 0x08, 0x8E);
    idt_set_entry(36, (unsigned int)irq4, 0x08, 0x8E);
    idt_set_entry(37, (unsigned int)irq5, 0x08, 0x8E);
    idt_set_entry(38, (unsigned int)irq6, 0x08, 0x8E);
    idt_set_entry(39, (unsigned int)irq7, 0x08, 0x8E);
    idt_set_entry(40, (unsigned int)irq8, 0x08, 0x8E);
    idt_set_entry(41, (unsigned int)irq9, 0x08, 0x8E);
    idt_set_entry(42, (unsigned int)irq10, 0x08, 0x8E);
    idt_set_entry(43, (unsigned int)irq11, 0x08, 0x8E);
    idt_set_entry(44, (unsigned int)irq12, 0x08, 0x8E);
    idt_set_entry(45, (unsigned int)irq13, 0x08, 0x8E);
    idt_set_entry(46, (unsigned int)irq14, 0x08, 0x8E);
    idt_set_entry(47, (unsigned int)irq15, 0x08, 0x8E);

    __asm__ volatile ("lidt %0" : : "m" (idt_ptr));

    irq_remap();

    __asm__ volatile ("sti");
}
