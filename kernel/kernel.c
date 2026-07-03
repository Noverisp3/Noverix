#include "drivers/screen.h"
#include "drivers/keyboard.h"
#include "drivers/serial.h"
#include "drivers/ata.h"
#include "cpu/ports.h"
#include "drivers/nvfs.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/timer.h"
#include "cpu/ports.h"
#include "memory/pfa.h"
#include "memory/paging.h"
#include "memory/heap.h"

#define debug_log(msg) serial_write_string("[kernel] "); serial_write_string(msg); serial_write_char('\n')

#define LINE_BUF 256
#define PROMPT_LEN 256
#define HISTORY_SIZE 16

static char history[HISTORY_SIZE][LINE_BUF];
static int history_count;

static int strlen(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

static void strcpy(char *dst, const char *src)
{
    if (!dst || !src) return;
    while ((*dst++ = *src++));
}

static int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

static void reboot(void)
{
    debug_log("reboot");
    while ((inb(0x64) & 0x02) != 0);
    outb(0x64, 0xFE);
}

static void shutdown(void)
{
    debug_log("shutdown");
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
    __asm__ volatile ("cli; hlt");
}

static void history_add(const char *buf)
{
    if (!buf[0]) return;
    if (history_count > 0 && strcmp(buf, history[history_count - 1]) == 0)
        return;
    if (history_count < HISTORY_SIZE) history_count++;
    for (int i = history_count - 1; i > 0; i--)
        strcpy(history[i], history[i - 1]);
    strcpy(history[0], buf);
}

static char read_char_any(void)
{
    while (1) {
        char c = get_char();
        if (c) return c;
        if (serial_data_available()) {
            c = serial_read_char();
            if (c == '\r') c = '\n';
            return c;
        }
    }
}

static void readline(char *buf, int max)
{
    int len = 0, pos = 0;
    if (!buf || max <= 0) return;
    buf[0] = 0;
    int hist_pos = -1;
    char saved[LINE_BUF];
    while (len < max - 1) {
        char c = read_char_any();
        if (pos > len) pos = len;
        if (c == KEY_UP && history_count > 0) {
            if (hist_pos == -1) {
                strcpy(saved, buf);
                hist_pos = 0;
            } else if (hist_pos < history_count - 1) {
                hist_pos++;
            } else {
                continue;
            }
            for (int i = 0; i < pos; i++) print_string("\b");
            for (int i = 0; i < len; i++) print_char(' ');
            for (int i = 0; i < len; i++) print_string("\b");
            strcpy(buf, history[hist_pos]);
            len = strlen(buf);
            pos = len;
            for (int i = 0; i < len; i++) print_char(buf[i]);
        } else if (c == KEY_DOWN) {
            if (hist_pos == -1) continue;
            hist_pos--;
            for (int i = 0; i < pos; i++) print_string("\b");
            for (int i = 0; i < len; i++) print_char(' ');
            for (int i = 0; i < len; i++) print_string("\b");
            if (hist_pos >= 0) {
                strcpy(buf, history[hist_pos]);
            } else {
                strcpy(buf, saved);
            }
            len = strlen(buf);
            pos = len;
            for (int i = 0; i < len; i++) print_char(buf[i]);
        } else if (c == KEY_LEFT) {
            if (pos > 0) { pos--; print_string("\b"); }
        } else if (c == KEY_RIGHT) {
            if (pos < len) { print_char(buf[pos]); pos++; }
        } else if (c == '\n') {
            print_char('\n');
            buf[len] = 0;
            history_add(buf);
            return;
        } else if (c == '\b') {
            if (pos > 0) {
                int old_pos = pos, old_len = len, j;
                for (j = pos - 1; j < len - 1; j++) buf[j] = buf[j + 1];
                len--; pos--;
                for (j = 0; j < old_pos; j++) print_string("\b");
                for (j = 0; j < old_len; j++) print_char(' ');
                for (j = 0; j < old_len; j++) print_string("\b");
                for (j = 0; j < len; j++) print_char(buf[j]);
                for (j = len; j > pos; j--) print_string("\b");
            }
        } else {
            int old_pos = pos, old_len = len, j;
            for (j = len; j > pos; j--) buf[j] = buf[j - 1];
            buf[pos] = c;
            len++; pos++;
            for (j = 0; j < old_pos; j++) print_string("\b");
            for (j = 0; j < old_len; j++) print_char(' ');
            for (j = 0; j < old_len; j++) print_string("\b");
            for (j = 0; j < len; j++) print_char(buf[j]);
            for (j = len; j > pos; j--) print_string("\b");
        }
    }
    buf[len] = 0;
}

static void handle_cmd(const char *buf)
{
    char cmd[LINE_BUF];
    char arg[LINE_BUF];
    int i = 0, j = 0;

    while (buf[i] && buf[i] == ' ') i++;
    while (buf[i] && buf[i] != ' ') cmd[j++] = buf[i++];
    cmd[j] = 0;

    while (buf[i] && buf[i] == ' ') i++;
    j = 0;
    while (buf[i]) arg[j++] = buf[i++];
    arg[j] = 0;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        print_string("Noveris OS Shell\n");
        print_string("----------------\n");
        print_string("help     Show this help\n");
        print_string("clear    Clear screen\n");
        print_string("echo     Print text or write file (echo text > file)\n");
        print_string("cat      Display file contents\n");
        print_string("ls       List files (ls, ls <dir>)\n");
        print_string("cd       Change directory\n");
        print_string("mkdir    Create directory\n");
        print_string("rmdir    Remove empty directory\n");
        print_string("rm       Delete file\n");
        print_string("hex      Print a number in hex\n");
        print_string("ver      Show version\n");
        print_string("sleep    Sleep for N milliseconds\n");
        print_string("ata      List ATA drives\n");
        print_string("crash    Trigger a crash (for testing)\n");
        print_string("reboot   Reboot system\n");
        print_string("shutdown Power off\n");
    } else if (strcmp(cmd, "echo") == 0) {
        char *redir = arg;
        while (*redir && *redir != '>') redir++;
        if (*redir == '>') {
            *redir = 0;
            char *content = arg;
            char *fname = redir + 1;
            while (*fname == ' ') fname++;
            if (fname[0]) {
                if (nvfs_write(fname, content, strlen(content)) == 0)
                    print_string("OK\n");
                else
                    print_string("FAIL\n");
            }
        } else {
            if (arg[0]) print_string(arg);
            print_string("\n");
        }
    } else if (strcmp(cmd, "clear") == 0) {
        clear_screen();
    } else if (strcmp(cmd, "hex") == 0) {
        unsigned int n = 0;
        int k = 0;
        while (arg[k]) {
            n = n * 10 + (arg[k] - '0');
            k++;
        }
        print_hex(n);
        print_string("\n");
    } else if (strcmp(cmd, "ver") == 0) {
        print_string("Noveris OS v0.1\n");
    } else if (strcmp(cmd, "sleep") == 0) {
        unsigned int n = 0;
        int k = 0;
        while (arg[k]) {
            n = n * 10 + (arg[k] - '0');
            k++;
        }
        if (n > 0 && n <= 10000) {
            print_string("Sleeping ");
            print_hex(n);
            print_string(" ms...\n");
            sleep_ms(n);
            print_string("Done.\n");
        } else {
            print_string("Usage: sleep <ms> (1-10000)\n");
        }
    } else if (strcmp(cmd, "ata") == 0) {
        int found = 0;
        for (int ch = 0; ch < 2; ch++) {
            for (int dr = 0; dr < 2; dr++) {
                if (ata_drive_exists(ch, dr)) {
                    print_string("ATA ");
                    print_hex(ch);
                    print_string(":");
                    print_string(dr ? "1 " : "0 ");
                    print_string(ata_get_model(ch, dr));
                    print_string("\n");
                    found = 1;
                }
            }
        }
        if (!found) print_string("No drives found.\n");
    } else if (strcmp(cmd, "cat") == 0) {
        if (arg[0]) {
            char tmp[512];
            int n = nvfs_read(arg, tmp, 511);
            if (n > 0) {
                tmp[n] = 0;
                print_string(tmp);
                print_char('\n');
            } else if (n == 0) {
                print_string("(empty)\n");
            } else {
                print_string("FAIL\n");
            }
        } else {
            print_string("Usage: cat <file>\n");
        }
    } else if (strcmp(cmd, "ls") == 0) {
        nvfs_list(arg[0] ? arg : "");
    } else if (strcmp(cmd, "cd") == 0) {
        if (nvfs_chdir(arg[0] ? arg : "", 0) != 0)
            print_string("Not found\n");
    } else if (strcmp(cmd, "mkdir") == 0) {
        if (arg[0]) {
            if (nvfs_mkdir(arg) == 0) print_string("OK\n");
            else print_string("FAIL\n");
        } else {
            print_string("Usage: mkdir <path>\n");
        }
    } else if (strcmp(cmd, "rmdir") == 0) {
        if (arg[0]) {
            if (nvfs_rmdir(arg) == 0) print_string("OK\n");
            else print_string("FAIL\n");
        } else {
            print_string("Usage: rmdir <path>\n");
        }
    } else if (strcmp(cmd, "rm") == 0) {
        if (arg[0]) {
            if (nvfs_delete(arg) == 0) print_string("OK\n");
            else print_string("FAIL\n");
        } else {
            print_string("Usage: rm <file>\n");
        }
    } else if (strcmp(cmd, "crash") == 0) {
        print_string("Triggering exception...\n");
        __asm__ volatile ("ud2");
    } else if (strcmp(cmd, "reboot") == 0) {
        print_string("Rebooting...\n");
        reboot();
    } else if (strcmp(cmd, "shutdown") == 0 || strcmp(cmd, "poweroff") == 0) {
        print_string("Shutting down...\n");
        shutdown();
    } else if (cmd[0]) {
        print_string("Unknown command: ");
        print_string(cmd);
        print_string("\n");
        debug_log("unknown command"); serial_write_string(": "); serial_write_string(cmd); serial_write_char('\n');
    }
}

void kernel_main(void)
{
    init_serial();
    init_gdt();
    init_idt();
    init_screen();
    init_keyboard();
    init_timer(100);
    pfa_init();
    init_paging();

    // Paging + PFA deep test
    {
        unsigned int cr0 = read_cr0(), cr3 = read_cr3();
        serial_write_string("[test] CR0="); serial_write_hex(cr0);
        serial_write_string(" CR3="); serial_write_hex(cr3);
        serial_write_string(" PG="); serial_write_hex(cr0 & 0x80000000 ? 1 : 0);
        serial_write_char('\n');

        unsigned int *phys = (unsigned int *)alloc_frame();
        serial_write_string("[test] alloc_frame="); serial_write_hex((unsigned int)phys);
        serial_write_char('\n');

        if (phys) {
            phys[0] = 0xDEADBEEF;
            phys[1] = 0xCAFEBABE;
            serial_write_string("[test] write 0xDEADBEEF @ "); serial_write_hex((unsigned int)&phys[0]);
            serial_write_string(" readback="); serial_write_hex(phys[0]);
            serial_write_char('\n');

            unsigned int test_virt = 0x00F00000;
            int r = map_page(test_virt, (unsigned int)phys, PAGE_WRITE);
            serial_write_string("[test] map_page("); serial_write_hex(test_virt);
            serial_write_string(" -> "); serial_write_hex((unsigned int)phys);
            serial_write_string(")="); serial_write_hex(r);
            serial_write_char('\n');

            unsigned int *virt = (unsigned int *)test_virt;
            serial_write_string("[test] virt read="); serial_write_hex(virt[0]);
            serial_write_string(" "); serial_write_hex(virt[1]);
            serial_write_char('\n');

            virt[0] = 0x12345678;
            serial_write_string("[test] phys read after virt write="); serial_write_hex(phys[0]);
            serial_write_char('\n');

            int ok = (phys[0] == 0x12345678 && phys[1] == 0xCAFEBABE);
            serial_write_string("[test] paging "); serial_write_string(ok ? "PASS" : "FAIL");
            serial_write_char('\n');

            free_frame(phys);
        }
    }

    heap_init();

    {
        serial_write_string("[test] heap malloc/free\n");
        char *a = (char *)malloc(32);
        char *b = (char *)malloc(64);
        char *c = (char *)malloc(128);
        if (a && b && c) {
            a[0] = 'H'; a[1] = 'e'; a[2] = 0;
            b[0] = 'W'; b[1] = 'o'; b[2] = 0;
            serial_write_string("[test] a="); serial_write_string(a);
            serial_write_string(" b="); serial_write_string(b);
            serial_write_string(" c="); serial_write_hex((unsigned int)c);
            serial_write_string(" free a+b -> alloc 96=");
            free(a); free(b);
            char *d = (char *)malloc(96);
            if (d) {
                d[0] = 'X';
                serial_write_string("OK d="); serial_write_hex((unsigned int)d);
            } else {
                serial_write_string("FAIL");
            }
            serial_write_char('\n');
            free(c); free(d);
        } else {
            serial_write_string("[test] heap FAIL\n");
        }
    }

    ata_init();
    if (nvfs_mount() == 0) {
        debug_log("NVFS mounted");
    } else {
        debug_log("NVFS mount failed");
    }

    debug_log("kernel_main started");

    clear_screen();
    print_string("Noveris OS v0.1\n");
    print_string("================\n");
    if (!nvfs_is_mounted())
        print_string("No filesystem disk - file operations disabled\n");
    print_string("Type 'help' for commands.\n\n");

    while (1) {
        char buf[LINE_BUF];
        char prompt[PROMPT_LEN];
        if (nvfs_path_string(nvfs_get_cwd(), prompt, PROMPT_LEN) == 0) {
            int p = 0;
            while (prompt[p]) p++;
            prompt[p] = '$'; prompt[p+1] = ' '; prompt[p+2] = 0;
        } else {
            prompt[0] = '$'; prompt[1] = ' '; prompt[2] = 0;
        }
        print_string(prompt);
        readline(buf, LINE_BUF);
        handle_cmd(buf);
    }
}
