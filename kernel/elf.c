#include "elf.h"
#include "drivers/nvfs.h"
#include "drivers/screen.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "lib.h"
#include "task.h"
#include "cpu/cpu.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "memory/pfa.h"
#include "memory/paging.h"
#include "memory/heap.h"

#define USER_ELF_BASE      0x00800000
#define USER_ELF_LIMIT     0x00900000
#define USER_STACK_TOP     0x00A00000
#define USER_STACK_SIZE    0x1000
#define USER_API_ADDR      0x009FF000
#define USER_ENTRY_TRAMP   0x009FFC00
#define USER_EXIT_TRAMP    0x009FFE00

#define SYS_EXIT 0

typedef struct {
    void (*clear_screen)(void);
    void (*print_char)(char c);
    void (*print_string)(const char *str);
    void (*print_hex)(unsigned int num);
    void (*print_int)(unsigned int num);
    int (*get_cursor_x)(void);
    int (*get_cursor_y)(void);
    void (*set_cursor)(int x, int y);

    char (*get_char)(void);
    char (*read_char)(void);

    int (*is_graphics_active)(void);
    void (*draw_pixel)(int x, int y, unsigned int color);
    void (*fill_rect)(int x, int y, int w, int h, unsigned int color);
    int (*fb_cols)(void);
    int (*fb_rows)(void);

    void (*sleep_ms)(unsigned int ms);
    unsigned int (*get_ticks)(void);

    void *(*malloc)(unsigned int size);
    void (*free)(void *ptr);

    int (*nvfs_read)(const char *path, void *out, unsigned max);
    int (*nvfs_write)(const char *path, const void *data, unsigned size);
    int (*nvfs_append)(const char *path, const void *data, unsigned size);
    int (*nvfs_delete)(const char *path);
    int (*nvfs_mkdir)(const char *path);
    int (*nvfs_rmdir)(const char *path);
    int (*nvfs_chdir)(const char *path, unsigned *out_inode);
    unsigned (*nvfs_get_cwd)(void);
    int (*nvfs_list)(const char *path);
    const char *(*nvfs_strerror)(int err);
    int (*nvfs_is_mounted)(void);
    void (*exit)(void);
} noverix_api_t;

extern int is_graphics_active(void);
extern void draw_pixel(int x, int y, unsigned int color);
extern void fill_rect(int x, int y, int w, int h, unsigned int color);
extern int fb_cols(void);
extern int fb_rows(void);
extern void sleep_ms(unsigned int ms);
extern unsigned int get_ticks(void);


static void user_exit(void)
{
    __asm__ volatile ("mov %0, %%eax; int $0x80" : : "i" (SYS_EXIT) : "eax");
}

static noverix_api_t kernel_api = {
    .clear_screen = clear_screen_user,
    .print_char = print_char_user,
    .print_string = print_string_user,
    .print_hex = print_hex_user,
    .print_int = print_int_user,
    .get_cursor_x = get_cursor_x,
    .get_cursor_y = get_cursor_y,
    .set_cursor = set_cursor_user,
    .get_char = get_char_user,
    .read_char = read_char_user,
    .is_graphics_active = is_graphics_active,
    .draw_pixel = draw_pixel,
    .fill_rect = fill_rect,
    .fb_cols = fb_cols,
    .fb_rows = fb_rows,
    .sleep_ms = sleep_ms,
    .get_ticks = get_ticks,
    .malloc = malloc_user,
    .free = free_user,
    .nvfs_read = nvfs_read,
    .nvfs_write = nvfs_write,
    .nvfs_append = nvfs_append,
    .nvfs_delete = nvfs_delete,
    .nvfs_mkdir = nvfs_mkdir,
    .nvfs_rmdir = nvfs_rmdir,
    .nvfs_chdir = nvfs_chdir,
    .nvfs_get_cwd = nvfs_get_cwd,
    .nvfs_list = nvfs_list,
    .nvfs_strerror = nvfs_strerror,
    .nvfs_is_mounted = nvfs_is_mounted,
    .exit = user_exit
};



static unsigned char elf_load_buf[128 * 1024];

static void free_task_resources(task_t *t)
{
    if (t->page_dir && t->page_dir != kernel_page_dir) {
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
    unsigned int syscall_num = regs->eax;

    switch (syscall_num) {
    case SYS_EXIT:
    {
        serial_write_string("[elf] user task exit\n");
        int cpu = get_cpu_id();
        task_t *curr = cpu_info[cpu].current_task;
        if (!curr) return 0;

        spinlock_lock(&sched_lock);
        free_task_resources(curr);
        curr->state = TASK_FREE;
        curr->cpu_assigned = -1;

        /* Pick next READY task (skip current) */
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

        /* No other task — switch to idle */
        cpu_info[cpu].current_task = 0;
        spinlock_unlock(&sched_lock);
        page_dir_switch(kernel_page_dir);
        return 0;

    found:
        next->state = TASK_RUNNING;
        next->cpu_assigned = cpu;
        cpu_info[cpu].current_task = next;
        spinlock_unlock(&sched_lock);

        gdt_set_kernel_stack(cpu, (unsigned int)next->kernel_stack_base + TASK_STACK_SIZE);
        if (next->page_dir != kernel_page_dir)
            page_dir_switch(next->page_dir);
        return next->kernel_esp;
    }
    default:
        break;
    }
    return 0;
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

    if (ehdr->e_phoff + ehdr->e_phnum * ehdr->e_phentsize > (unsigned int)size) {
        print_string("Error: Program Header Table out of file bounds\n");
        return -1;
    }

    if (ehdr->e_phentsize < sizeof(elf32_phdr_t)) {
        print_string("Error: Program header entry size too small\n");
        return -1;
    }

    /* Map user memory region (8MB-10MB) with PAGE_USER */
    for (unsigned int addr = USER_ELF_BASE; addr < USER_STACK_TOP; addr += 0x1000) {
        if (map_page(addr, addr, PAGE_WRITE | PAGE_USER) != 0) {
            print_string("Error: Failed to map user memory\n");
            return -1;
        }
    }

    /* Load ELF segments */
    unsigned char *ph_table = elf_load_buf + ehdr->e_phoff;
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

            if (phdr->p_vaddr < USER_ELF_BASE || phdr->p_vaddr >= USER_ELF_LIMIT) {
                print_string("Error: Invalid ELF load address\n");
                return -1;
            }

            if (phdr->p_vaddr + phdr->p_memsz > USER_STACK_TOP) {
                print_string("Error: ELF segment extends past user range\n");
                return -1;
            }

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

    /* Copy API struct to user-accessible memory */
    noverix_api_t *user_api = (noverix_api_t *)USER_API_ADDR;
    lib_memcpy(user_api, &kernel_api, sizeof(noverix_api_t));

    /* Exit trampoline at USER_EXIT_TRAMP: mov $SYS_EXIT, %eax; int $0x80 */
    {
        unsigned char *t = (unsigned char *)USER_EXIT_TRAMP;
        t[0] = 0xB8; t[1] = SYS_EXIT; t[2] = 0; t[3] = 0; t[4] = 0;
        t[5] = 0xCD; t[6] = 0x80;
    }

    /* No trampoline needed — user app sets segments in inline asm at main() entry.
       Set up user stack with return address and api pointer for main(). */
    unsigned int *usp = (unsigned int *)USER_STACK_TOP;
    *--usp = (unsigned int)user_api;       /* main's argument */
    *--usp = (unsigned int)USER_EXIT_TRAMP; /* return address */

    /* Create user task */
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

    /* Ring 3 IRET frame (same layout as interrupt from ring 3) */
    *--sp = 0x23;                      /* user SS (GDT_UDATA | RPL3) */
    *--sp = (unsigned int)usp;         /* user ESP                    */
    *--sp = 0x202;                     /* user EFLAGS (IF=1)          */
    *--sp = 0x1B;                      /* user CS (GDT_UCODE | RPL3)  */
    *--sp = ehdr->e_entry;             /* user EIP -> ELF entry point (main) */
    *--sp = 0;                         /* err_code (skipped)       */
    *--sp = 0;                         /* int_no (skipped)         */
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;  /* pusha: EAX, ECX, EDX, EBX */
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;  /* pusha: ESP, EBP, ESI, EDI */
    *--sp = 0x23; *--sp = 0x23; *--sp = 0x23; *--sp = 0x23; /* DS, ES, FS, GS */

    t->kernel_esp = (unsigned int)sp;

    t->page_dir = kernel_page_dir;
    t->state = TASK_READY;

    /* Add to ready list */
    spinlock_lock(&sched_lock);
    if (!ready_head) {
        ready_head = t;
        t->next = t;
    } else {
        t->next = ready_head->next;
        ready_head->next = t;
    }
    spinlock_unlock(&sched_lock);

    serial_write_string("[elf] User task created. Entry=");
    serial_write_hex(ehdr->e_entry);
    serial_write_string(", Tramp=");
    serial_write_hex(USER_ENTRY_TRAMP);
    serial_write_string("\n");

    print_string("User task started. PID: ");
    print_int(t->pid);
    print_string("\n");

    return 0;
}
