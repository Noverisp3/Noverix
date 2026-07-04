#ifndef IDT_H
#define IDT_H

typedef struct {
    unsigned int gs, fs, es, ds;
    unsigned int edi, esi, ebp, esp, ebx, edx, ecx, eax;
    unsigned int int_no, err_code;
    unsigned int eip, cs, eflags, useresp, ss;
} __attribute__((packed)) registers_t;

typedef void (*interrupt_handler_t)(registers_t *);

void init_idt(void);
void register_interrupt_handler(int irq, interrupt_handler_t handler);
void idt_install(void);

#endif
