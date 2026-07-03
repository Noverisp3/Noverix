// FILE: kernel/elf.c

#include "elf.h"
#include "drivers/nvfs.h"
#include "drivers/screen.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"

typedef struct
{
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
} noverix_api_t;

extern int is_graphics_active(void);
extern void draw_pixel(int x, int y, unsigned int color);
extern void fill_rect(int x, int y, int w, int h, unsigned int color);
extern int fb_cols(void);
extern int fb_rows(void);
extern void sleep_ms(unsigned int ms);
extern unsigned int get_ticks(void);
extern void *malloc(unsigned int size);
extern void free(void *ptr);

static noverix_api_t kernel_api = {
    // Screen / Console
    .clear_screen = clear_screen,
    .print_char = print_char,
    .print_string = print_string,
    .print_hex = print_hex,
    .print_int = print_int,
    .get_cursor_x = get_cursor_x,
    .get_cursor_y = get_cursor_y,
    .set_cursor = set_cursor,

    // Keyboard
    .get_char = get_char,
    .read_char = read_char,

    // Graphics
    .is_graphics_active = is_graphics_active,
    .draw_pixel = draw_pixel,
    .fill_rect = fill_rect,
    .fb_cols = fb_cols,
    .fb_rows = fb_rows,

    // Timer
    .sleep_ms = sleep_ms,
    .get_ticks = get_ticks,

    // Heap Memory
    .malloc = malloc,
    .free = free,

    // Filesystem (NVFS)
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
    .nvfs_is_mounted = nvfs_is_mounted};

static unsigned char elf_load_buf[128 * 1024];

static void elf_memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--)
        *d++ = *s++;
}

static void elf_memset(void *dst, int v, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--)
        *d++ = (unsigned char)v;
}

static void serial_write_int(unsigned int num)
{
    char buf[12];
    int i = 11;
    buf[11] = 0;
    if (num == 0)
    {
        serial_write_string("0");
        return;
    }
    while (num && i > 0)
    {
        i--;
        buf[i] = '0' + (num % 10);
        num /= 10;
    }
    serial_write_string(buf + i);
}

int elf_exec(const char *path)
{
    int size = nvfs_read(path, elf_load_buf, sizeof(elf_load_buf));
    if (size <= 0)
    {
        print_string("Error: Failed to read file or file is empty\n");
        return -1;
    }

    serial_write_string("[elf] File read successfully. Size: ");
    serial_write_int(size);
    serial_write_string(" bytes\n");

    if (size < (int)sizeof(elf32_ehdr_t))
    {
        print_string("Error: File too small to contain ELF Header\n");
        return -1;
    }

    elf32_ehdr_t *ehdr = (elf32_ehdr_t *)elf_load_buf;

    serial_write_string("[elf] Checking ELF Magic... ");
    if (*(unsigned int *)ehdr->e_ident != ELF_MAGIC)
    {
        serial_write_string("FAILED!\n");
        print_string("Error: Not a valid ELF file\n");
        return -1;
    }
    serial_write_string("OK\n");

    serial_write_string("[elf] Checking ELF Class (32-bit)... ");
    if (ehdr->e_ident[4] != 1)
    { // 1 = ELFCLASS32
        serial_write_string("FAILED!\n");
        print_string("Error: 64-bit ELF not supported. Please compile as 32-bit!\n");
        return -1;
    }
    serial_write_string("OK\n");

    serial_write_string("[elf] Checking Machine Architecture (x86)... ");
    if (ehdr->e_machine != 3)
    { // 3 = x86
        serial_write_string("FAILED!\n");
        print_string("Error: Not an x86 executable\n");
        return -1;
    }
    serial_write_string("OK\n");

    serial_write_string("[elf] Entry Point (Symbol main): ");
    serial_write_hex(ehdr->e_entry);
    serial_write_string("\n");

    serial_write_string("[elf] Program Header Table Offset: ");
    serial_write_hex(ehdr->e_phoff);
    serial_write_string(", Number of Headers: ");
    serial_write_int(ehdr->e_phnum);
    serial_write_string("\n");

    if (ehdr->e_phoff + ehdr->e_phnum * ehdr->e_phentsize > (unsigned int)size)
    {
        print_string("Error: Program Header Table out of file bounds\n");
        return -1;
    }

    unsigned char *ph_table = elf_load_buf + ehdr->e_phoff;
    for (int i = 0; i < ehdr->e_phnum; i++)
    {
        elf32_phdr_t *phdr = (elf32_phdr_t *)(ph_table + i * ehdr->e_phentsize);

        serial_write_string("[elf] Segment ");
        serial_write_int(i);
        serial_write_string(": Type=");
        serial_write_hex(phdr->p_type);

        if (phdr->p_type == PT_LOAD)
        {
            serial_write_string(" (PT_LOAD)\n");
            serial_write_string("      Offset in file: ");
            serial_write_hex(phdr->p_offset);
            serial_write_string(" -> Target VAddr: ");
            serial_write_hex(phdr->p_vaddr);
            serial_write_string("\n");
            serial_write_string("      FileSize: ");
            serial_write_int(phdr->p_filesz);
            serial_write_string(", MemSize: ");
            serial_write_int(phdr->p_memsz);
            serial_write_string("\n");

            if (phdr->p_vaddr < 0x00100000 || phdr->p_vaddr >= 0x02000000)
            {
                print_string("Error: Invalid ELF load address ");
                print_hex(phdr->p_vaddr);
                print_string("\n");
                return -1;
            }

            if (phdr->p_memsz > 1024 * 1024 || phdr->p_filesz > 1024 * 1024)
            {
                print_string("Error: ELF segment too large\n");
                return -1;
            }

            if (phdr->p_offset + phdr->p_filesz > (unsigned int)size)
            {
                print_string("Error: Segment data out of file bounds\n");
                return -1;
            }

            unsigned char *dest = (unsigned char *)phdr->p_vaddr;
            unsigned char *src = elf_load_buf + phdr->p_offset;

            serial_write_string("      >> Copying ");
            serial_write_int(phdr->p_filesz);
            serial_write_string(" bytes to ");
            serial_write_hex(phdr->p_vaddr);
            serial_write_string("\n");

            elf_memcpy(dest, src, phdr->p_filesz);

            if (phdr->p_memsz > phdr->p_filesz)
            {
                unsigned int zero_size = phdr->p_memsz - phdr->p_filesz;
                serial_write_string("      >> Zeroing BSS (");
                serial_write_int(zero_size);
                serial_write_string(" bytes) at ");
                serial_write_hex(phdr->p_vaddr + phdr->p_filesz);
                serial_write_string("\n");

                elf_memset(dest + phdr->p_filesz, 0, zero_size);
            }
        }
        else
        {
            serial_write_string(" (IGNORED)\n");
        }
    }

    void (*entry_point)(noverix_api_t *) = (void (*)(noverix_api_t *))ehdr->e_entry;
    serial_write_string("[elf] Jumping to ");
    serial_write_hex((unsigned int)entry_point);
    serial_write_char('\n');

    entry_point(&kernel_api);

    return 0;
}