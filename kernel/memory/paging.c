#include "paging.h"
#include "pfa.h"
#include "../drivers/serial.h"

typedef unsigned int page_dir_entry_t;
typedef unsigned int page_table_entry_t;

#define PDE_IDX(virt) (((unsigned int)(virt) >> 22) & 0x3FF)
#define PTE_IDX(virt) (((unsigned int)(virt) >> 12) & 0x3FF)

static page_dir_entry_t *page_dir;

static page_table_entry_t *create_table(unsigned int virt, unsigned int flags)
{
    page_table_entry_t *table = (page_table_entry_t *)alloc_frame();
    if (!table) return 0;
    for (int i = 0; i < 1024; i++)
        table[i] = 0;
    unsigned int pde_idx = PDE_IDX(virt);
    page_dir[pde_idx] = ((unsigned int)table) | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
    return table;
}

void init_paging(void)
{
    serial_write_string("[paging] init\n");

    page_dir = (page_dir_entry_t *)alloc_frame();
    if (!page_dir) {
        serial_write_string("[paging] no memory for PD\n");
        return;
    }

    for (int i = 0; i < 1024; i++)
        page_dir[i] = 0;

    serial_write_string("[paging] identity map 0-4MB\n");

    page_table_entry_t *first_pt = (page_table_entry_t *)alloc_frame();
    if (!first_pt) return;
    for (int i = 0; i < 1024; i++)
        first_pt[i] = (i * FRAME_SIZE) | PAGE_PRESENT | PAGE_WRITE;

    page_dir[0] = ((unsigned int)first_pt) | PAGE_PRESENT | PAGE_WRITE;

    serial_write_string("[paging] map 4-32MB\n");

    for (unsigned int virt = 0x00400000; virt < MAX_MEMORY; virt += 0x400000) {
        page_table_entry_t *pt = create_table(virt, 0);
        if (!pt) return;
        for (int i = 0; i < 1024; i++)
            pt[i] = (virt + i * FRAME_SIZE) | PAGE_PRESENT | PAGE_WRITE;
    }

    serial_write_string("[paging] enable\n");

    __asm__ volatile (
        "mov %0, %%cr3\n"
        "mov %%cr0, %%eax\n"
        "or $0x80000000, %%eax\n"
        "mov %%eax, %%cr0\n"
        : : "r" (page_dir) : "eax", "memory"
    );

    serial_write_string("[paging] enabled\n");
}
