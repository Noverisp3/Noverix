#include "gdt.h"
#include "ports.h"

typedef struct {
    unsigned short limit_low;
    unsigned short base_low;
    unsigned char base_middle;
    unsigned char access;
    unsigned char granularity;
    unsigned char base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed)) gdt_ptr_t;

static gdt_entry_t gdt_entries[5];
static gdt_ptr_t gdt_ptr;

static void gdt_set_entry(int num, unsigned int base, unsigned int limit,
                          unsigned char access, unsigned char gran)
{
    gdt_entries[num].base_low = base & 0xFFFF;
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;
    gdt_entries[num].limit_low = limit & 0xFFFF;
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access = access;
}

void init_gdt(void)
{
    gdt_ptr.limit = sizeof(gdt_entries) - 1;
    gdt_ptr.base = (unsigned int)&gdt_entries;

    gdt_set_entry(0, 0, 0, 0, 0);                      /* null */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);       /* code */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF);       /* data */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);       /* user code */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);       /* user data */

    __asm__ volatile ("lgdt %0" : : "m" (gdt_ptr));

    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%ss\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "ljmp $0x08, $.__flush\n"
        ".__flush:\n"
        : : : "eax", "memory"
    );
}
