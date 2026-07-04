#include "paging.h"
#include "pfa.h"
#include "../drivers/serial.h"
#include "../sync/sync.h"

typedef unsigned int page_dir_entry_t;
typedef unsigned int page_table_entry_t;

#define PDE_IDX(virt) (((unsigned int)(virt) >> 22) & 0x3FF)
#define PTE_IDX(virt) (((unsigned int)(virt) >> 12) & 0x3FF)

static page_dir_entry_t *page_dir;
static spinlock_t paging_lock = SPINLOCK_INIT;

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

void init_paging(unsigned int detected_ram)
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

    serial_write_string("[paging] identity map 4MB-");
    serial_write_hex(detected_ram);
    serial_write_char('\n');

    for (unsigned int virt = 0x00400000; virt < detected_ram; virt += 0x400000) {
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

int map_page_impl(unsigned int virt, unsigned int phys, unsigned int flags)
{
    unsigned int pde_idx = PDE_IDX(virt);

    if (!(page_dir[pde_idx] & PAGE_PRESENT)) {
        if (!create_table(virt, flags)) return -1;
    }

    page_table_entry_t *table = (page_table_entry_t *)(page_dir[pde_idx] & 0xFFFFF000);
    unsigned int pte_idx = PTE_IDX(virt);
    table[pte_idx] = (phys & 0xFFFFF000) | PAGE_PRESENT | (flags & (PAGE_WRITE | PAGE_USER));

    __asm__ volatile ("invlpg (%0)" : : "r" (virt) : "memory");
    return 0;
}

int map_page(unsigned int virt, unsigned int phys, unsigned int flags)
{
    unsigned int flags_save = spinlock_lock_irqsave(&paging_lock);
    int r = map_page_impl(virt, phys, flags);
    spinlock_unlock_irqrestore(&paging_lock, flags_save);
    return r;
}

int unmap_page_impl(unsigned int virt)
{
    unsigned int pde_idx = PDE_IDX(virt);
    if (!(page_dir[pde_idx] & PAGE_PRESENT))
        return -1;

    page_table_entry_t *table = (page_table_entry_t *)(page_dir[pde_idx] & 0xFFFFF000);
    unsigned int pte_idx = PTE_IDX(virt);
    if (!(table[pte_idx] & PAGE_PRESENT))
        return -1;

    table[pte_idx] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r" (virt) : "memory");
    return 0;
}

int unmap_page(unsigned int virt)
{
    unsigned int flags = spinlock_lock_irqsave(&paging_lock);
    int r = unmap_page_impl(virt);
    spinlock_unlock_irqrestore(&paging_lock, flags);
    return r;
}

int get_page_mapping(unsigned int virt, unsigned int *phys_out)
{
    unsigned int flags = spinlock_lock_irqsave(&paging_lock);
    unsigned int pde_idx = PDE_IDX(virt);
    if (!(page_dir[pde_idx] & PAGE_PRESENT)) {
        spinlock_unlock_irqrestore(&paging_lock, flags);
        return -1;
    }

    page_table_entry_t *table = (page_table_entry_t *)(page_dir[pde_idx] & 0xFFFFF000);
    unsigned int pte_idx = PTE_IDX(virt);
    if (!(table[pte_idx] & PAGE_PRESENT)) {
        spinlock_unlock_irqrestore(&paging_lock, flags);
        return -1;
    }

    if (phys_out)
        *phys_out = table[pte_idx] & 0xFFFFF000;
    spinlock_unlock_irqrestore(&paging_lock, flags);
    return 0;
}

unsigned int read_cr3(void)
{
    unsigned int val;
    __asm__ volatile ("mov %%cr3, %0" : "=r" (val));
    return val;
}

unsigned int read_cr0(void)
{
    unsigned int val;
    __asm__ volatile ("mov %%cr0, %0" : "=r" (val));
    return val;
}

void dump_page_info(void)
{
    unsigned int flags = spinlock_lock_irqsave(&paging_lock);
    unsigned int pd_phys = read_cr3();
    serial_write_string("[pages] Page Dir at ");
    serial_write_hex(pd_phys);
    serial_write_string("\n");

    int total_pdes = 0, total_ptes = 0;
    for (int pde_i = 0; pde_i < 1024; pde_i++)
    {
        if (!(page_dir[pde_i] & PAGE_PRESENT))
            continue;
        total_pdes++;
        unsigned int pt_phys = page_dir[pde_i] & 0xFFFFF000;
        page_table_entry_t *table = (page_table_entry_t *)pt_phys;
        int ptes = 0;
        for (int pte_i = 0; pte_i < 1024; pte_i++)
        {
            if (!(table[pte_i] & PAGE_PRESENT))
                continue;
            ptes++;
            total_ptes++;
        }
        serial_write_string("  PDE[");
        serial_write_int(pde_i);
        serial_write_string("] PT=");
        serial_write_hex(pt_phys);
        serial_write_string(" (");
        serial_write_int(ptes);
        serial_write_string(" PTEs, virt 0x");
        serial_write_hex(pde_i << 22);
        serial_write_string(")\n");
    }
    serial_write_string("  Total: ");
    serial_write_int(total_pdes);
    serial_write_string(" PDEs, ");
    serial_write_int(total_ptes);
    serial_write_string(" PTEs (");
    serial_write_int(total_ptes * 4);
    serial_write_string(" KB mapped)\n");
    spinlock_unlock_irqrestore(&paging_lock, flags);
}
