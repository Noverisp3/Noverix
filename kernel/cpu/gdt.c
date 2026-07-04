#include "gdt.h"
#include "../cpu/cpu.h"

typedef struct {
    unsigned short limit;
    unsigned int base;
} __attribute__((packed)) gdt_ptr_t;

/* TSS must be 104 bytes (0x68), packed */
typedef struct {
    unsigned int link;
    unsigned int esp0;
    unsigned int ss0;
    unsigned int esp1;
    unsigned int ss1;
    unsigned int esp2;
    unsigned int ss2;
    unsigned int cr3;
    unsigned int eip;
    unsigned int eflags;
    unsigned int eax, ecx, edx, ebx;
    unsigned int esp, ebp, esi, edi;
    unsigned int es, cs, ss, ds, fs, gs;
    unsigned int ldtr;
    unsigned short reserved;
    unsigned short iopb;
} __attribute__((packed)) tss_t;

/* Bootstrap GDT (5 entries for early boot) */
static gdt_entry_t boot_gdt[5];
static gdt_ptr_t boot_gdt_ptr;

/* Per-CPU GDTs and TSSs */
gdt_entry_t per_cpu_gdt[MAX_CPU][GDT_ENTRIES];    /* non-static for debugging */
static gdt_ptr_t per_cpu_gdt_ptr[MAX_CPU];
static tss_t per_cpu_tss[MAX_CPU];

static void gdt_set_entry_at(gdt_entry_t *gdt, int num,
                             unsigned int base, unsigned int limit,
                             unsigned char access, unsigned char gran)
{
    gdt[num].base_low    = base & 0xFFFF;
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = limit & 0xFFFF;
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

static void reload_segments(void)
{
    __asm__ volatile (
        "mov %0, %%eax\n"
        "mov %%eax, %%ds\n"
        "mov %%eax, %%ss\n"
        "mov %%eax, %%es\n"
        "mov %%eax, %%fs\n"
        "ljmp %1, $1f\n"
        "1:\n"
        :
        : "r" (GDT_DATA), "i" (GDT_CODE)
        : "eax", "memory"
    );
}

void init_gdt(void)
{
    boot_gdt_ptr.limit = sizeof(boot_gdt) - 1;
    boot_gdt_ptr.base  = (unsigned int)boot_gdt;

    gdt_set_entry_at(boot_gdt, 0, 0, 0, 0, 0);                    /* null      */
    gdt_set_entry_at(boot_gdt, 1, 0, 0xFFFFFFFF, 0x9A, 0xCF);     /* code      */
    gdt_set_entry_at(boot_gdt, 2, 0, 0xFFFFFFFF, 0x92, 0xCF);     /* data      */
    gdt_set_entry_at(boot_gdt, 3, 0, 0xFFFFFFFF, 0xFA, 0xCF);     /* user code */
    gdt_set_entry_at(boot_gdt, 4, 0, 0xFFFFFFFF, 0xF2, 0xCF);     /* user data */

    __asm__ volatile ("lgdt %0" : : "m" (boot_gdt_ptr));
    reload_segments();
}

void gdt_init_percpu(int cpu_id)
{
    gdt_entry_t *gdt = per_cpu_gdt[cpu_id];
    gdt_ptr_t *ptr   = &per_cpu_gdt_ptr[cpu_id];
    tss_t *tss       = &per_cpu_tss[cpu_id];

    /* Copy shared entries (null, code, data, ucode, udata) */
    for (int i = 0; i < 5; i++)
        gdt[i] = boot_gdt[i];

    /* TSS descriptor (entry 5, selector 0x28) */
    {
        unsigned int tss_base = (unsigned int)tss;
        unsigned int tss_limit = sizeof(tss_t) - 1;  /* 103 (0x67) */
        gdt_set_entry_at(gdt, 5, tss_base, tss_limit, 0x89, 0x00);
    }

    /* Per-CPU data segment (entry 6, selector 0x30) */
    {
        unsigned int cpu_base = (unsigned int)&cpu_info[cpu_id];
        unsigned int cpu_limit = sizeof(cpu_info_t) - 1;
        gdt_set_entry_at(gdt, 6, cpu_base, cpu_limit, 0x92, 0x00);
    }

    /* Initialize TSS */
    tss->ss0   = GDT_DATA;         /* kernel data segment */
    tss->esp0  = 0;                /* set by stack init */
    tss->cs    = GDT_CODE;         /* default CS for task switch */
    tss->ds    = GDT_DATA;
    tss->es    = GDT_DATA;
    tss->fs    = GDT_DATA;
    tss->gs    = GDT_PERCPU;
    tss->ss    = GDT_DATA;
    tss->iopb  = sizeof(tss_t);    /* disable I/O bitmap */

    /* Load this GDT */
    ptr->limit = sizeof(gdt_entry_t) * GDT_ENTRIES - 1;
    ptr->base  = (unsigned int)gdt;
    __asm__ volatile ("lgdt %0" : : "m" (*ptr));

    reload_segments();

    /* Load task register */
    __asm__ volatile ("ltr %%ax" : : "a" (GDT_TSS));

    /* Set GS to per-CPU data selector */
    __asm__ volatile (
        "mov %0, %%eax\n"
        "mov %%eax, %%gs"
        : : "r" (GDT_PERCPU) : "eax"
    );
}

void gdt_set_kernel_stack(int cpu_id, unsigned int esp0)
{
    if (cpu_id >= 0 && cpu_id < MAX_CPU)
        per_cpu_tss[cpu_id].esp0 = esp0;
}
