#ifndef ELF_H
#define ELF_H

#define ELF_MAGIC 0x464C457F
#define PT_LOAD 1

#define SYS_EXIT             0
#define SYS_CLEAR_SCREEN     1
#define SYS_PRINT_CHAR       2
#define SYS_PRINT_STRING     3
#define SYS_PRINT_HEX        4
#define SYS_PRINT_INT        5
#define SYS_SET_CURSOR       6
#define SYS_GET_CHAR         7
#define SYS_READ_CHAR        8
#define SYS_IS_GRAPHICS      9
#define SYS_DRAW_PIXEL       10
#define SYS_FILL_RECT        11
#define SYS_FB_COLS          12
#define SYS_FB_ROWS          13
#define SYS_SLEEP_MS         14
#define SYS_GET_TICKS        15
#define SYS_MALLOC           16
#define SYS_FREE             17
#define SYS_NVFS_READ        18
#define SYS_NVFS_WRITE       19
#define SYS_NVFS_APPEND      20
#define SYS_NVFS_DELETE      21
#define SYS_NVFS_MKDIR       22
#define SYS_NVFS_RMDIR       23
#define SYS_NVFS_CHDIR       24
#define SYS_NVFS_GET_CWD     25
#define SYS_NVFS_LIST        26
#define SYS_NVFS_STRERROR    27
#define SYS_NVFS_IS_MOUNTED  28
#define SYS_YIELD            29

typedef struct {
    unsigned char e_ident[16];
    unsigned short e_type;
    unsigned short e_machine;
    unsigned int e_version;
    unsigned int e_entry;
    unsigned int e_phoff;
    unsigned int e_shoff;
    unsigned int e_flags;
    unsigned short e_ehsize;
    unsigned short e_phentsize;
    unsigned short e_phnum;
    unsigned short e_shentsize;
    unsigned short e_shnum;
    unsigned short e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    unsigned int p_type;
    unsigned int p_offset;
    unsigned int p_vaddr;
    unsigned int p_paddr;
    unsigned int p_filesz;
    unsigned int p_memsz;
    unsigned int p_flags;
    unsigned int p_align;
} __attribute__((packed)) elf32_phdr_t;

#include "cpu/idt.h"

int elf_exec(const char *path);
unsigned int syscall_handler(registers_t *regs);

#endif
