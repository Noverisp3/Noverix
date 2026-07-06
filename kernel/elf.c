#include "elf.h"
#include "drivers/nvfs.h"
#include "drivers/screen.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/graphics.h"
#include "lib.h"
#include "task.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/timer.h"
#include "memory/pfa.h"
#include "memory/paging.h"
#include "memory/heap.h"

#define USER_ELF_BASE      0x00800000
#define USER_ELF_LIMIT     0x00900000
#define USER_STACK_TOP     0x00A00000
#define USER_STACK_SIZE    0x1000
#define USER_EXIT_TRAMP    0x009FFE00
#define USER_PDE_IDX       2

static unsigned char elf_load_buf[128 * 1024];

static void free_task_resources(task_t *t)
{
    if (t->page_dir && t->page_dir != kernel_page_dir) {
        for (int i = 0; i < 1024; i++) {
            if (t->page_dir[i] == kernel_page_dir[i])
                continue;
            if (t->page_dir[i] & PAGE_PRESENT) {
                page_table_entry_t *pt = (page_table_entry_t *)(t->page_dir[i] & 0xFFFFF000);
                for (int j = 0; j < 1024; j++) {
                    if (pt[j] & PAGE_PRESENT)
                        free_frame((void *)(pt[j] & 0xFFFFF000));
                }
                free_frame((void *)pt);
            }
        }
        free_frame((void *)t->page_dir);
        t->page_dir = kernel_page_dir;
    }

    if (t->kernel_stack_base) {
        free_frames(t->kernel_stack_base, TASK_STACK_SIZE >> 12);
        t->kernel_stack_base = 0;
    }
}

unsigned int syscall_handler(registers_t *regs)
{
    unsigned int num = regs->eax;

    switch (num) {
    case SYS_EXIT:
    {
        serial_write_string("[elf] user task exit\n");
        int cpu = get_cpu_id();
        task_t *curr = cpu_info[cpu].current_task;
        if (!curr) return 0;

        unsigned int _flags2 = spinlock_lock_irqsave(&sched_lock);
        free_task_resources(curr);
        curr->state = TASK_FREE;
        curr->cpu_assigned = -1;

        task_t *next = ready_head;
        task_t *scan = next;
        if (scan) {
            do {
                if (scan->state == TASK_READY && scan->cpu_assigned < 0 &&
                    scan->kernel_esp != 0 && scan != curr) {
                    next = scan;
                    goto found;
                }
                scan = scan->next;
            } while (scan != ready_head);
        }

        cpu_info[cpu].current_task = 0;
        spinlock_unlock_irqrestore(&sched_lock, _flags2);
        page_dir_switch(kernel_page_dir);
        return 0;

    found:
        next->state = TASK_RUNNING;
        next->cpu_assigned = cpu;
        cpu_info[cpu].current_task = next;
        spinlock_unlock_irqrestore(&sched_lock, _flags2);

        gdt_set_kernel_stack(cpu, (unsigned int)next->kernel_stack_base + TASK_STACK_SIZE);
        page_dir_switch(next->page_dir);
        return next->kernel_esp;
    }

    case SYS_CLEAR_SCREEN:
        clear_screen();
        regs->eax = 0;
        return (unsigned int)regs;

    case SYS_PRINT_CHAR:
        print_char((char)regs->ebx);
        regs->eax = 0;
        return (unsigned int)regs;

    case SYS_PRINT_STRING:
        print_string((const char *)regs->ebx);
        regs->eax = 0;
        return (unsigned int)regs;

    case SYS_PRINT_HEX:
        print_hex(regs->ebx);
        regs->eax = 0;
        return (unsigned int)regs;

    case SYS_PRINT_INT:
        print_int(regs->ebx);
        regs->eax = 0;
        return (unsigned int)regs;

    case SYS_SET_CURSOR:
        set_cursor((int)regs->ebx, (int)regs->ecx);
        regs->eax = 0;
        return (unsigned int)regs;

    case SYS_GET_CHAR:
        regs->eax = (unsigned int)(unsigned char)get_char();
        return (unsigned int)regs;

    case SYS_READ_CHAR:
        regs->eax = (unsigned int)(unsigned char)read_char();
        return (unsigned int)regs;

    case SYS_IS_GRAPHICS:
        regs->eax = is_graphics_active() ? 1 : 0;
        return (unsigned int)regs;

    case SYS_DRAW_PIXEL:
        draw_pixel((int)regs->ebx, (int)regs->ecx, regs->edx);
        regs->eax = 0;
        return (unsigned int)regs;

    case SYS_FILL_RECT:
        fill_rect((int)regs->ebx, (int)regs->ecx, (int)regs->edx,
                  (int)regs->esi, regs->edi);
        regs->eax = 0;
        return (unsigned int)regs;

    case SYS_FB_COLS:
        regs->eax = (unsigned int)fb_cols();
        return (unsigned int)regs;

    case SYS_FB_ROWS:
        regs->eax = (unsigned int)fb_rows();
        return (unsigned int)regs;

    case SYS_SLEEP_MS:
    {
        unsigned int ms = regs->ebx;
        unsigned int wake_ticks = get_ticks() + (ms + 9) / 10;
        unsigned int new_esp = task_block_and_switch((unsigned int)regs, wake_ticks);
        return new_esp;
    }

    case SYS_YIELD:
    {
        unsigned int new_esp = task_yield((unsigned int)regs);
        if (new_esp)
            return new_esp;
        regs->eax = 0;
        return (unsigned int)regs;
    }

    case SYS_GET_TICKS:
        regs->eax = get_ticks();
        return (unsigned int)regs;

    case SYS_MALLOC:
        regs->eax = (unsigned int)malloc_user(regs->ebx);
        return (unsigned int)regs;

    case SYS_FREE:
        free_user((void *)regs->ebx);
        regs->eax = 0;
        return (unsigned int)regs;

    case SYS_NVFS_READ:
        regs->eax = (unsigned int)nvfs_read((const char *)regs->ebx,
                                            (void *)regs->ecx, regs->edx);
        return (unsigned int)regs;

    case SYS_NVFS_WRITE:
        regs->eax = (unsigned int)nvfs_write((const char *)regs->ebx,
                                             (const void *)regs->ecx, regs->edx);
        return (unsigned int)regs;

    case SYS_NVFS_APPEND:
        regs->eax = (unsigned int)nvfs_append((const char *)regs->ebx,
                                              (const void *)regs->ecx, regs->edx);
        return (unsigned int)regs;

    case SYS_NVFS_DELETE:
        regs->eax = (unsigned int)nvfs_delete((const char *)regs->ebx);
        return (unsigned int)regs;

    case SYS_NVFS_MKDIR:
        regs->eax = (unsigned int)nvfs_mkdir((const char *)regs->ebx);
        return (unsigned int)regs;

    case SYS_NVFS_RMDIR:
        regs->eax = (unsigned int)nvfs_rmdir((const char *)regs->ebx);
        return (unsigned int)regs;

    case SYS_NVFS_CHDIR:
        regs->eax = (unsigned int)nvfs_chdir((const char *)regs->ebx,
                                             (unsigned *)regs->ecx);
        return (unsigned int)regs;

    case SYS_NVFS_GET_CWD:
        regs->eax = nvfs_get_cwd();
        return (unsigned int)regs;

    case SYS_NVFS_LIST:
        regs->eax = (unsigned int)nvfs_list((const char *)regs->ebx);
        return (unsigned int)regs;

    case SYS_NVFS_STRERROR:
        regs->eax = (unsigned int)nvfs_strerror((int)regs->ebx);
        return (unsigned int)regs;

    case SYS_NVFS_IS_MOUNTED:
        regs->eax = nvfs_is_mounted() ? 1 : 0;
        return (unsigned int)regs;

    default:
        serial_write_string("[elf] unknown syscall: ");
        serial_write_hex(num);
        serial_write_string("\n");
        regs->eax = (unsigned int)-1;
        return (unsigned int)regs;
    }
}

int elf_exec(const char *path)
{
    int size = nvfs_read(path, elf_load_buf, sizeof(elf_load_buf));
    if (size <= 0) {
        print_string("Error: Failed to read file\n");
        return -1;
    }

    serial_write_string("[elf] File read. Size: ");
    serial_write_int(size);
    serial_write_string(" bytes\n");

    if (size < (int)sizeof(elf32_ehdr_t)) {
        print_string("Error: File too small\n");
        return -1;
    }

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)elf_load_buf;

    serial_write_string("[elf] Checking ELF Magic... ");
    if (*(unsigned int *)ehdr->e_ident != ELF_MAGIC) {
        serial_write_string("FAILED!\n");
        print_string("Error: Not a valid ELF file\n");
        return -1;
    }
    serial_write_string("OK\n");

    serial_write_string("[elf] Checking ELF Class (32-bit)... ");
    if (ehdr->e_ident[4] != 1) {
        serial_write_string("FAILED!\n");
        print_string("Error: 64-bit ELF not supported\n");
        return -1;
    }
    serial_write_string("OK\n");

    serial_write_string("[elf] Checking Machine Architecture (x86)... ");
    if (ehdr->e_machine != 3) {
        serial_write_string("FAILED!\n");
        print_string("Error: Not an x86 executable\n");
        return -1;
    }
    serial_write_string("OK\n");

    serial_write_string("[elf] Entry Point: ");
    serial_write_hex(ehdr->e_entry);
    serial_write_string("\n");

    if (ehdr->e_entry < USER_ELF_BASE || ehdr->e_entry >= USER_ELF_LIMIT) {
        print_string("Error: Invalid ELF entry point\n");
        return -1;
    }

    serial_write_string("[elf] Program Header Table Offset: ");
    serial_write_hex(ehdr->e_phoff);
    serial_write_string(", Number of Headers: ");
    serial_write_int(ehdr->e_phnum);
    serial_write_string("\n");

    if (ehdr->e_phnum > 0xFFFF || ehdr->e_phentsize > 0xFFFF ||
        (unsigned int)ehdr->e_phnum * (unsigned int)ehdr->e_phentsize > 0xFFFFFFFFU - ehdr->e_phoff) {
        print_string("Error: Program header table overflow\n");
        return -1;
    }
    if (ehdr->e_phoff + ehdr->e_phnum * ehdr->e_phentsize > (unsigned int)size) {
        print_string("Error: Program Header Table out of file bounds\n");
        return -1;
    }

    if (ehdr->e_phentsize < sizeof(elf32_phdr_t)) {
        print_string("Error: Program header entry size too small\n");
        return -1;
    }

    unsigned char *ph_table = elf_load_buf + ehdr->e_phoff;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        elf32_phdr_t *phdr = (elf32_phdr_t *)(ph_table + i * ehdr->e_phentsize);
        if (phdr->p_type != PT_LOAD)
            continue;

        if (phdr->p_vaddr < USER_ELF_BASE || phdr->p_vaddr >= USER_ELF_LIMIT) {
            print_string("Error: Invalid ELF load address\n");
            return -1;
        }

        if (phdr->p_vaddr + phdr->p_memsz > USER_STACK_TOP) {
            print_string("Error: ELF segment extends past user range\n");
            return -1;
        }

        if (phdr->p_offset + phdr->p_filesz < phdr->p_offset ||
            phdr->p_offset + phdr->p_filesz > (unsigned int)size) {
            print_string("Error: ELF segment extends past file\n");
            return -1;
        }

        if (phdr->p_filesz > phdr->p_memsz) {
            print_string("Error: p_filesz > p_memsz\n");
            return -1;
        }
    }

    page_dir_t pd = page_dir_create();
    if (!pd) {
        print_string("Error: Failed to allocate page directory\n");
        return -1;
    }

    pd[USER_PDE_IDX] = 0;

    for (unsigned int addr = USER_ELF_BASE; addr < USER_STACK_TOP; addr += 0x1000) {
        unsigned int phys = (unsigned int)alloc_frame();
        if (!phys) {
            print_string("Error: Out of memory for user pages\n");
            pd[USER_PDE_IDX] = 0;
            free_frame((void *)pd);
            return -1;
        }
        if (map_page_to_dir(pd, addr, phys, PAGE_WRITE | PAGE_USER) != 0) {
            print_string("Error: Failed to map user page\n");
            pd[USER_PDE_IDX] = 0;
            free_frame((void *)pd);
            return -1;
        }
    }

    page_dir_switch(pd);

    for (int i = 0; i < ehdr->e_phnum; i++) {
        elf32_phdr_t *phdr = (elf32_phdr_t *)(ph_table + i * ehdr->e_phentsize);

        serial_write_string("[elf] Segment ");
        serial_write_int(i);
        serial_write_string(": Type=");
        serial_write_hex(phdr->p_type);

        if (phdr->p_type == PT_LOAD) {
            serial_write_string(" (PT_LOAD)\n");
            serial_write_string("      Offset: ");
            serial_write_hex(phdr->p_offset);
            serial_write_string(" -> VAddr: ");
            serial_write_hex(phdr->p_vaddr);
            serial_write_string("\n");
            serial_write_string("      FileSize: ");
            serial_write_int(phdr->p_filesz);
            serial_write_string(", MemSize: ");
            serial_write_int(phdr->p_memsz);
            serial_write_string("\n");

            unsigned char *dest = (unsigned char *)phdr->p_vaddr;
            unsigned char *src = elf_load_buf + phdr->p_offset;

            serial_write_string("      >> Copying ");
            serial_write_int(phdr->p_filesz);
            serial_write_string(" bytes\n");

            lib_memcpy(dest, src, phdr->p_filesz);

            if (phdr->p_memsz > phdr->p_filesz) {
                unsigned int zero_size = phdr->p_memsz - phdr->p_filesz;
                serial_write_string("      >> Zeroing BSS (");
                serial_write_int(zero_size);
                serial_write_string(" bytes)\n");
                lib_memset(dest + phdr->p_filesz, 0, zero_size);
            }
        } else {
            serial_write_string(" (IGNORED)\n");
        }
    }

    /* Exit trampoline: mov $0, %eax; int $0x80 */
    {
        unsigned char *t = (unsigned char *)USER_EXIT_TRAMP;
        t[0] = 0xB8; t[1] = 0; t[2] = 0; t[3] = 0; t[4] = 0;
        t[5] = 0xCD; t[6] = 0x80;
    }

    /* Set up user stack with return address to exit trampoline */
    unsigned int *usp = (unsigned int *)USER_STACK_TOP;
    *--usp = (unsigned int)USER_EXIT_TRAMP; /* return address if main() returns */

    page_dir_switch(kernel_page_dir);

    /* Remove kernel identity-mapped PDEs so user code can't access
     * physical memory below 8 MB through stack underflow or direct
     * address manipulation.  All kernel API calls go through int 0x80. */
    pd[0] = 0;
    pd[1] = 0;

    task_t *t = alloc_task();
    if (!t) {
        print_string("Error: Failed to allocate task\n");
        return -1;
    }

    void *stack = alloc_frames(TASK_STACK_SIZE >> 12);
    if (!stack) {
        free_frame(t);
        print_string("Error: Failed to allocate kernel stack\n");
        return -1;
    }
    t->kernel_stack_base = stack;

    unsigned int *sp = (unsigned int *)((unsigned int)stack + TASK_STACK_SIZE);

    *--sp = 0x23;                      /* user SS */
    *--sp = (unsigned int)usp;         /* user ESP */
    *--sp = 0x202;                     /* user EFLAGS (IF=1) */
    *--sp = 0x1B;                      /* user CS */
    *--sp = ehdr->e_entry;             /* user EIP */
    *--sp = 0;                         /* err_code */
    *--sp = 0;                         /* int_no */
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0x23; *--sp = 0x23; *--sp = 0x23; *--sp = 0x23;

    t->kernel_esp = (unsigned int)sp;

    {
        unsigned int *fp = (unsigned int *)t->kernel_esp;
        serial_write_string("[elf] task kernel_esp=");
        serial_write_hex(t->kernel_esp);
        serial_write_string(" base=");
        serial_write_hex((unsigned int)stack);
        serial_write_string(" entry=");
        serial_write_hex(ehdr->e_entry);
        serial_write_string(" frame_eip=");
        serial_write_hex(fp[14]);
        serial_write_string("\n");
    }

    t->page_dir = pd;
    t->state = TASK_READY;

    unsigned int _flags = spinlock_lock_irqsave(&sched_lock);
    if (!ready_head) {
        ready_head = t;
        t->next = t;
    } else {
        t->next = ready_head->next;
        ready_head->next = t;
    }
    spinlock_unlock_irqrestore(&sched_lock, _flags);

    serial_write_string("[elf] User task created. Entry=");
    serial_write_hex(ehdr->e_entry);
    serial_write_string("\n");

    print_string("User task started. PID: ");
    print_int(t->pid);
    print_string("\n");

    return 0;
}
