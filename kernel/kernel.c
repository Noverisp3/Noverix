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
#include "drivers/graphics.h"
#include "elf.h"
#include "lib.h"
#include "sync/sync.h"
#include "apic/lapic.h"
#include "apic/ioapic.h"
#include "acpi/acpi.h"
#include "cpu/cpu.h"
#include "ap_startup.h"
#include "scheduler/scheduler.h"
#include "sync/tlb.h"

#define VBE_INFO_ADDR ((volatile unsigned int *)0x1000)

#define debug_log(msg) serial_write_string("[kernel] "); serial_write_string(msg); serial_write_char('\n')

#define LINE_BUF 256
#define PROMPT_LEN 256
#define HISTORY_SIZE 16

#define UINT_MAX 4294967295U

static char history[HISTORY_SIZE][LINE_BUF];
static int history_count;

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

static char pipe_data[4096];
static int has_pipe_data;

    static void history_add(const char *buf)
    {
        if (!buf[0]) return;
        if (history_count > 0 && lib_strcmp(buf, history[history_count - 1]) == 0)
            return;
        if (history_count >= HISTORY_SIZE) {
            for (int i = 0; i < HISTORY_SIZE - 1; i++)
                lib_strcpy(history[i], history[i + 1]);
            history_count = HISTORY_SIZE - 1;
            lib_strcpy(history[history_count], buf);
            return;
        }
        if (history_count < HISTORY_SIZE) history_count++;
        for (int i = history_count - 1; i > 0; i--)
            lib_strcpy(history[i], history[i - 1]);
        lib_strcpy(history[0], buf);
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
    int start_x = get_cursor_x();
    int start_y = get_cursor_y();

    while (len < max - 1) {
        char c = read_char_any();
        if (pos > len) pos = len;
        if (c == KEY_UP && history_count > 0) {
            if (hist_pos == -1) {
                lib_strcpy(saved, buf);
                hist_pos = 0;
            } else if (hist_pos < history_count - 1) {
                hist_pos++;
            } else {
                continue;
            }
            set_cursor(start_x, start_y);
            for (int i = 0; i < len; i++) print_char(' ');
            set_cursor(start_x, start_y);
            lib_strcpy(buf, history[hist_pos]);
            len = lib_strlen(buf);
            pos = len;
            for (int i = 0; i < len; i++) print_char(buf[i]);
        } else if (c == KEY_DOWN) {
            if (hist_pos == -1) continue;
            hist_pos--;
            set_cursor(start_x, start_y);
            for (int i = 0; i < len; i++) print_char(' ');
            set_cursor(start_x, start_y);
            if (hist_pos >= 0) {
                lib_strcpy(buf, history[hist_pos]);
            } else {
                lib_strcpy(buf, saved);
            }
            len = lib_strlen(buf);
            pos = len;
            for (int i = 0; i < len; i++) print_char(buf[i]);
        } else if (c == KEY_LEFT) {
            if (pos > 0) { pos--; print_string("\b"); }
        } else if (c == KEY_RIGHT) {
            if (pos < len) { print_char(buf[pos]); pos++; }
        } else if (c == 3) {
            print_string("^C\n");
            buf[0] = 0;
            return;
        } else if (c == '\n') {
            print_char('\n');
            buf[len] = 0;
            history_add(buf);
            return;
        } else if (c == '\b') {
            if (pos > 0) {
                for (int i = pos - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--; pos--;
                set_cursor(start_x, start_y);
                for (int i = 0; i < len; i++) print_char(buf[i]);
                print_char(' ');
                int vp = start_x + pos;
                set_cursor(vp % 80, start_y + vp / 80);
            }
        } else if (len < max - 2) {
            for (int i = len; i > pos; i--) buf[i] = buf[i - 1];
            buf[pos] = c;
            len++; pos++;
            set_cursor(start_x, start_y);
            for (int i = 0; i < len; i++) print_char(buf[i]);
            int vp = start_x + pos;
            set_cursor(vp % 80, start_y + vp / 80);
        }
    }
    buf[len] = 0;
}

/* ── SMP scheduler test ── */

struct worker_result {
    volatile int sum;
    volatile int cpu_id;
};

struct worker_arg {
    int start;
    int count;
    struct worker_result *result;
};

static void sum_worker(void *arg)
{
    struct worker_arg *w = (struct worker_arg *)arg;
    int s = 0;
    for (int i = 0; i < w->count; i++)
        s += w->start + i;
    w->result->sum = s;
    w->result->cpu_id = get_cpu_id();
}

static void execute_cmd(const char *cmd, char *arg)
{
    if (lib_strcmp(cmd, "help") == 0 || lib_strcmp(cmd, "?") == 0) {
        print_string("Noverix Shell\n");
        print_string("-------------\n");
        print_string("help     Show this help\n");
        print_string("clear    Clear screen\n");
        print_string("echo     Print text or write file (echo text > file)\n");
        print_string("cat      Display file contents (cat reads pipe when no file)\n");
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
        print_string("exec     Run ELF executable\n");
        print_string("heap     Dump heap allocator state\n");
        print_string("mem      Show physical memory info\n");
        print_string("pages    Show page directory/table info\n");
        print_string("|        Pipe: cmd1 | cmd2 (output of cmd1 goes to cmd2)\n");
        print_string("rate     Set keyboard repeat rate: rate <delay:0-3> <rate:0-31>\n");
        print_string("smp      Run N parallel tasks across all CPUs\n");
        print_string("cpus     Show CPU info\n");
    } else if (lib_strcmp(cmd, "echo") == 0) {
        int is_append = 0;
        char *redir = arg;
        while (redir < arg + LINE_BUF - 1 && *redir && *redir != '>') redir++;
        if (redir >= arg + LINE_BUF - 1) {
            print_string("Path too long\n");
            return;
        }
        if (*redir == '>') {
            if (*(redir + 1) == '>') {
                is_append = 1;
                *(redir + 1) = 0;
            }
            *redir = 0;
            char *content = arg;
            char *fname = redir + 1 + is_append;
            while (*fname == ' ') fname++;
            if (fname[0]) {
                int r = is_append ? nvfs_append(fname, content, lib_strlen(content))
                                  : nvfs_write(fname, content, lib_strlen(content));
                if (r == 0)
                    print_string("OK\n");
                else {
                    print_string(nvfs_strerror(nvfs_errno));
                    print_string("\n");
                }
            }
        } else {
            if (arg[0]) {
                print_string(arg);
                print_string("\n");
            } else if (has_pipe_data) {
                print_string(pipe_data);
            } else {
                print_string("\n");
            }
        }
    } else if (lib_strcmp(cmd, "clear") == 0) {
        clear_screen();
    } else if (lib_strcmp(cmd, "hex") == 0) {
        unsigned int n = 0;
        int k = 0;
        while (arg[k]) {
            if (k >= 8 || n > UINT_MAX / 10 || (n == UINT_MAX / 10 && (unsigned int)(arg[k] - '0') > UINT_MAX % 10)) {
                print_string("Invalid number\n");
                return;
            }
            n = n * 10 + (arg[k] - '0');
            k++;
        }
        print_hex(n);
        print_string("\n");
    } else if (lib_strcmp(cmd, "ver") == 0) {
        print_string("Noverix v0.1\n");
    } else if (lib_strcmp(cmd, "sleep") == 0) {
        unsigned int n = 0;
        int k = 0;
        while (arg[k]) {
            if (k >= 7 || n > UINT_MAX / 10 || (n == UINT_MAX / 10 && (unsigned int)(arg[k] - '0') > UINT_MAX % 10)) {
                print_string("Invalid number\n");
                return;
            }
            n = n * 10 + (arg[k] - '0');
            k++;
        }
        if (n > 0 && n <= 10000) {
            print_string("Sleeping ");
            print_int(n);
            print_string(" ms...\n");
            sleep_ms(n);
            print_string("Done.\n");
        } else {
            print_string("Usage: sleep <ms> (1-10000)\n");
        }
    } else if (lib_strcmp(cmd, "ata") == 0) {
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
    } else if (lib_strcmp(cmd, "cat") == 0) {
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
                print_string(nvfs_strerror(nvfs_errno));
                print_string("\n");
            }
        } else if (has_pipe_data) {
            print_string(pipe_data);
        } else {
            print_string("Usage: cat <file>\n");
        }
    } else if (lib_strcmp(cmd, "ls") == 0) {
        nvfs_list(arg[0] ? arg : "");
    } else if (lib_strcmp(cmd, "cd") == 0) {
        if (nvfs_chdir(arg[0] ? arg : "", 0) != 0) {
            print_string(nvfs_strerror(nvfs_errno));
            print_string("\n");
        }
    } else if (lib_strcmp(cmd, "mkdir") == 0) {
        if (arg[0]) {
            if (nvfs_mkdir(arg) == 0) print_string("OK\n");
            else { print_string(nvfs_strerror(nvfs_errno)); print_string("\n"); }
        } else {
            print_string("Usage: mkdir <path>\n");
        }
    } else if (lib_strcmp(cmd, "rmdir") == 0) {
        if (arg[0]) {
            if (nvfs_rmdir(arg) == 0) print_string("OK\n");
            else { print_string(nvfs_strerror(nvfs_errno)); print_string("\n"); }
        } else {
            print_string("Usage: rmdir <path>\n");
        }
    } else if (lib_strcmp(cmd, "rm") == 0) {
        if (arg[0]) {
            if (nvfs_delete(arg) == 0) print_string("OK\n");
            else { print_string(nvfs_strerror(nvfs_errno)); print_string("\n"); }
        } else {
            print_string("Usage: rm <file>\n");
        }
    } else if (lib_strcmp(cmd, "crash") == 0) {
        print_string("Triggering exception...\n");
        __asm__ volatile ("ud2");
    } else if (lib_strcmp(cmd, "reboot") == 0) {
        print_string("Rebooting...\n");
        reboot();
    } else if (lib_strcmp(cmd, "shutdown") == 0 || lib_strcmp(cmd, "poweroff") == 0) {
        print_string("Shutting down...\n");
        shutdown();
    } else if (lib_strcmp(cmd, "exec") == 0) {
        if (arg[0]) {
            if (elf_exec(arg) != 0) {
                print_string("Execution failed\n");
            }
        } else {
            print_string("Usage: exec <file>\n");
        }
    } else if (lib_strcmp(cmd, "heap") == 0) {
        heap_walk();
    } else if (lib_strcmp(cmd, "mem") == 0) {
        unsigned total = MAX_MEMORY;
        unsigned used = total / FRAME_SIZE - get_free_frame_count();
        unsigned free_frames = get_free_frame_count();
        unsigned total_kb = total / 1024;
        unsigned free_kb = free_frames * FRAME_SIZE / 1024;
        print_string("Total memory: ");
        if (total_kb >= 1024) { print_int(total_kb / 1024); print_string(" MB ("); print_int(total_kb); print_string(" KB)\n"); }
        else { print_int(total_kb); print_string(" KB\n"); }
        print_string("Frame size:   "); print_int(FRAME_SIZE); print_string(" bytes\n");
        print_string("Total frames: "); print_int(total / FRAME_SIZE); print_string("\n");
        print_string("Used frames:  "); print_int(used); print_string(" (");
        if (total) { unsigned pct = used * 100 / (total / FRAME_SIZE); print_int(pct); print_string("%");
        } else print_string("0%");
        print_string(")\n");
        print_string("Free frames:  "); print_int(free_frames); print_string(" (");
        if (total) { unsigned pct = free_frames * 100 / (total / FRAME_SIZE); print_int(pct); print_string("%");
        } else print_string("0%");
        print_string(")\n");
        print_string("Free memory:  ");
        if (free_kb >= 1024) { print_int(free_kb / 1024); print_string(" MB ("); print_int(free_kb); print_string(" KB)\n"); }
        else { print_int(free_kb); print_string(" KB\n"); }
    } else if (lib_strcmp(cmd, "pages") == 0) {
        dump_page_info();
    } else if (lib_strcmp(cmd, "rate") == 0) {
        unsigned char p = keyboard_get_typematic();
        unsigned char delay = (p >> 5) & 3;
        unsigned char rate = p & 0x1F;
        if (arg[0]) {
            unsigned int d = 0, r = 0;
            int k = 0;
            while (arg[k] >= '0' && arg[k] <= '9') {
                d = d * 10 + (arg[k] - '0');
                k++;
            }
            if (k == 0 || arg[k] != ' ') {
                print_string("Usage: rate <delay:0-3> <rate:0-31>\n");
                return;
            }
            while (arg[k] == ' ') k++;
            while (arg[k] >= '0' && arg[k] <= '9') {
                r = r * 10 + (arg[k] - '0');
                k++;
            }
            if (d > 3 || r > 31) {
                print_string("delay 0-3, rate 0-31 (0=fastest)\n");
                return;
            }
            keyboard_set_typematic((unsigned char)((d << 5) | r));
            print_string("OK\n");
        } else {
            print_string("delay="); print_int(delay);
            print_string(" (");
            switch (delay) {
                case 0: print_string("250ms"); break;
                case 1: print_string("500ms"); break;
                case 2: print_string("750ms"); break;
                case 3: print_string("1000ms"); break;
            }
            print_string("), rate="); print_int(rate);
            print_string(" (");
            unsigned int hz = 250 / (8 + rate);
            print_int(hz);
            print_string(" Hz");
            if (rate <= 1) print_string(", fastest");
            else if (rate <= 10) print_string(", fast");
            else if (rate <= 20) print_string(", medium");
            else print_string(", slow");
            print_string(")\n");
        }
    } else if (lib_strcmp(cmd, "smp") == 0) {
        int ntasks = 4;
        int per = 100000;
        struct worker_arg args[4];
        struct worker_result results[4];

        print_string("Submitting ");
        print_int(ntasks);
        print_string(" tasks (sum 1..");
        print_int(per);
        print_string(" each)...\n");

        for (int i = 0; i < ntasks; i++) {
            args[i].start = 1;
            args[i].count = per;
            results[i].sum = 0;
            results[i].cpu_id = -1;
            args[i].result = &results[i];
            scheduler_submit(sum_worker, &args[i]);
        }

        scheduler_wait_all();

        print_string("Results:\n");
        for (int i = 0; i < ntasks; i++) {
            print_string("  task ");
            print_int(i);
            print_string(": sum=");
            print_int((unsigned int)results[i].sum);
            print_string(" (cpu ");
            print_int((unsigned int)results[i].cpu_id);
            print_string(")\n");
        }
    } else if (lib_strcmp(cmd, "cpus") == 0) {
        print_string("CPUs: ");
        print_int(cpu_count);
        print_string("\n");
        for (int i = 0; i < cpu_count; i++) {
            print_string("  CPU ");
            print_int(i);
            print_string(" APIC ");
            print_int(cpu_info[i].apic_id);
            print_string(" state=");
            print_int(cpu_info[i].state);
            print_string("\n");
        }
    } else if (cmd[0]) {
        print_string("Unknown command: ");
        print_string(cmd);
        print_string("\n");
        debug_log("unknown command"); serial_write_string(": "); serial_write_string(cmd); serial_write_char('\n');
    }
}

static void handle_cmd(const char *buf)
{
    int pipe_idx = -1;
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == '|') {
            pipe_idx = i;
            break;
        }
    }

    if (pipe_idx >= 0) {
        char left[LINE_BUF];
        int j;
        for (j = 0; j < pipe_idx && j < LINE_BUF - 1; j++)
            left[j] = buf[j];
        left[j] = 0;
        while (j > 0 && left[j - 1] == ' ') left[--j] = 0;

        char right[LINE_BUF];
        int k = pipe_idx + 1;
        int r;
        for (r = 0; buf[k] && r < LINE_BUF - 1; k++, r++)
            right[r] = buf[k];
        right[r] = 0;
        r = 0;
        while (right[r] == ' ') r++;
        if (r > 0) {
            int s;
            for (s = 0; right[s + r]; s++)
                right[s] = right[s + r];
            right[s] = 0;
        }

        char cmd1[LINE_BUF], arg1[LINE_BUF];
        int i1 = 0, j1 = 0;
        while (left[i1] && left[i1] == ' ') i1++;
        while (left[i1] && left[i1] != ' ') cmd1[j1++] = left[i1++];
        cmd1[j1] = 0;
        while (left[i1] && left[i1] == ' ') i1++;
        j1 = 0;
        while (left[i1]) arg1[j1++] = left[i1++];
        arg1[j1] = 0;

        char cmd2[LINE_BUF], arg2[LINE_BUF];
        int i2 = 0, j2 = 0;
        while (right[i2] && right[i2] == ' ') i2++;
        while (right[i2] && right[i2] != ' ') cmd2[j2++] = right[i2++];
        cmd2[j2] = 0;
        while (right[i2] && right[i2] == ' ') i2++;
        j2 = 0;
        while (right[i2]) arg2[j2++] = right[i2++];
        arg2[j2] = 0;

        if (!cmd1[0]) {
            print_string("Syntax error: no command before pipe\n");
            return;
        }
        if (!cmd2[0]) {
            print_string("Syntax error: no command after pipe\n");
            return;
        }

        set_capture(1);
        execute_cmd(cmd1, arg1);
        set_capture(0);

        const char *captured = get_capture();
        int p;
        for (p = 0; captured[p] && p < (int)sizeof(pipe_data) - 1; p++)
            pipe_data[p] = captured[p];
        pipe_data[p] = 0;
        has_pipe_data = 1;

        execute_cmd(cmd2, arg2);
        has_pipe_data = 0;
    } else {
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

        execute_cmd(cmd, arg);
    }
}

void kernel_main(void)
{
    init_serial();

    unsigned int detected_ram = *(volatile unsigned int *)0x100C;
    serial_write_string("[boot] detected RAM=");
    serial_write_hex(detected_ram);
    serial_write_string(" (");
    serial_write_int(detected_ram / (1024 * 1024));
    serial_write_string(" MB)\n");

    init_gdt();
    init_idt();
    init_screen();
    init_keyboard();
    init_timer(100);
    pfa_init(detected_ram);
    init_paging(detected_ram);

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

    {
        serial_write_string("[test] sync primitives\n");
        spinlock_t lock;
        spinlock_init(&lock);
        spinlock_lock(&lock);
        spinlock_unlock(&lock);
        unsigned int f = spinlock_lock_irqsave(&lock);
        spinlock_unlock_irqrestore(&lock, f);
        serial_write_string("  spinlock OK\n");

        volatile unsigned int counter = 0;
        atomic_inc(&counter);
        atomic_inc(&counter);
        atomic_inc(&counter);
        atomic_dec(&counter);
        serial_write_string("  atomic_inc/dec="); serial_write_int(counter);
        serial_write_string(counter == 2 ? " OK\n" : " FAIL\n");

        unsigned int v = atomic_cmpxchg(&counter, 2, 99);
        serial_write_string("  cmpxchg(old=2->99)="); serial_write_int(v);
        serial_write_string(v == 2 ? " OK\n" : " FAIL\n");

        v = atomic_cmpxchg(&counter, 2, 88);
        serial_write_string("  cmpxchg(old=2->88)="); serial_write_int(v);
        serial_write_string(v == 99 ? " OK\n" : " FAIL\n");
        serial_write_string("  counter="); serial_write_int(counter);
        serial_write_string(counter == 99 ? " OK\n" : " FAIL\n");

        unsigned int x = atomic_xchg(&counter, 42);
        serial_write_string("  xchg(->42) old="); serial_write_int(x);
        serial_write_string(x == 99 ? " OK\n" : " FAIL\n");
        serial_write_string("  counter="); serial_write_int(counter);
        serial_write_string(counter == 42 ? " OK\n" : " FAIL\n");
    }

    {
        serial_write_string("[test] ACPI discovery\n");
        int n = acpi_parse_madt();
        if (n > 0) {
            serial_write_string("[test] ACPI PASS\n");
        } else {
            serial_write_string("[test] ACPI FAIL (no MADT)\n");
        }
    }

    {
        serial_write_string("[test] per-CPU init\n");
        gdt_init_percpu(0);
        gdt_set_kernel_stack(0, 0x90000);
        int my_id = get_cpu_id();
        serial_write_string("  BSP cpu_id="); serial_write_int(my_id);
        serial_write_string(my_id == 0 ? " OK\n" : " FAIL (expected 0)\n");
    }

    {
        serial_write_string("[test] APIC init\n");
        register_interrupt_handler(0xFF, spurious_handler);
        lapic_init();
        ioapic_init();
        tlb_init();
        serial_write_string("[test] APIC done\n");
    }

    start_aps();
    scheduler_init();

    // ── VBE init ──
    {
        volatile unsigned char *vbe = (volatile unsigned char *)0x1000;
        unsigned int lfb = *(volatile unsigned int *)vbe;
        unsigned short vbe_w = *(volatile unsigned short *)(vbe + 4);
        unsigned short vbe_h = *(volatile unsigned short *)(vbe + 6);
        unsigned short vbe_p = *(volatile unsigned short *)(vbe + 8);
        unsigned short vbe_bpp = *(volatile unsigned char *)(vbe + 10);
        serial_write_string("[vbe] lfb="); serial_write_hex(lfb);
        serial_write_string(" w="); serial_write_hex(vbe_w);
        serial_write_string(" h="); serial_write_hex(vbe_h);
        serial_write_string(" p="); serial_write_hex(vbe_p);
        serial_write_string(" bpp="); serial_write_hex(vbe_bpp);
        serial_write_char('\n');
        if (lfb && vbe_w && vbe_h) {
            unsigned int fb_size = vbe_h * vbe_p;
            unsigned int pages = (fb_size + 0xFFF) / 0x1000;
            serial_write_string("[vbe] mapping "); serial_write_hex(pages);
            serial_write_string(" pages for framebuffer\n");
            for (unsigned int i = 0; i < pages; i++) {
                map_page(lfb + i * 0x1000, lfb + i * 0x1000, PAGE_WRITE);
            }
            init_graphics(lfb, vbe_w, vbe_h, vbe_p, vbe_bpp);
            clear_screen();
            serial_write_string("[vbe] graphics mode active\n");
        } else {
            serial_write_string("[vbe] VBE not available, staying in text mode\n");
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

    {
        serial_write_string("[test] calloc\n");
        char *z = (char *)calloc(1, 64);
        if (z) {
            int ok = 1;
            for (int i = 0; i < 64; i++)
                if (z[i]) { ok = 0; break; }
            serial_write_string(ok ? "  zeroed OK\n" : "  NOT ZEROED!\n");
            free(z);
        } else {
            serial_write_string("  FAIL\n");
        }
    }

    {
        serial_write_string("[test] realloc\n");
        char *r = (char *)malloc(16);
        if (r) {
            for (int i = 0; i < 15; i++) r[i] = 'A' + i;
            r[15] = 0;
            char *rr = (char *)realloc(r, 64);
            if (rr) {
                int ok = 1;
                for (int i = 0; i < 15; i++)
                    if (rr[i] != (char)('A' + i)) { ok = 0; break; }
                serial_write_string(ok ? "  data preserved OK\n" : "  DATA CORRUPTED!\n");
                free(rr);
            } else {
                serial_write_string("  realloc FAIL\n");
            }
        }
    }

    {
        serial_write_string("[test] heap_walk\n");
        heap_walk();
    }

    {
        serial_write_string("[test] PFA free frames: ");
        serial_write_int(get_free_frame_count());
        serial_write_string("\n");
    }

    {
        serial_write_string("[test] PFA alloc 3 contiguous frames... ");
        unsigned int *cf = (unsigned int *)alloc_frames(3);
        if (cf) {
            serial_write_string("OK at ");
            serial_write_hex((unsigned int)cf);
            serial_write_string("\n");
            cf[0] = 0xAA; cf[512] = 0xBB; cf[1024] = 0xCC;
            free_frames(cf, 3);
            serial_write_string("[test] PFA free_frames OK\n");
        } else {
            serial_write_string("FAIL\n");
        }
    }

    {
        serial_write_string("[test] paging map_page + get_page_mapping... ");
        unsigned int *phys = (unsigned int *)alloc_frame();
        if (phys) {
            unsigned int test_virt = 0x00F00000;
            map_page(test_virt, (unsigned int)phys, PAGE_WRITE);
            unsigned int mapped_phys = 0;
            int r = get_page_mapping(test_virt, &mapped_phys);
            if (r == 0 && mapped_phys == ((unsigned int)phys & 0xFFFFF000)) {
                serial_write_string("OK (virt=");
                serial_write_hex(test_virt);
                serial_write_string(" -> phys=");
                serial_write_hex(mapped_phys);
                serial_write_string(")\n");
            } else {
                serial_write_string("FAIL\n");
            }
            free_frame(phys);
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
    print_string("Noverix v0.1\n");
    print_string("============\n");
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
