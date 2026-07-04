#ifndef GDT_H
#define GDT_H

#define GDT_NULL      0x00
#define GDT_CODE      0x08
#define GDT_DATA      0x10
#define GDT_UCODE     0x18
#define GDT_UDATA     0x20
#define GDT_TSS       0x28
#define GDT_PERCPU    0x30

#define GDT_ENTRIES   7

typedef struct {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed)) gdt_entry_t;

void init_gdt(void);
void gdt_init_percpu(int cpu_id);
void gdt_set_kernel_stack(int cpu_id, unsigned int esp0);
extern gdt_entry_t per_cpu_gdt[8][GDT_ENTRIES];

#endif
