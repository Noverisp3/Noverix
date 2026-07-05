# CODEBASE INDEX — Noverix OS

## 1. OVERALL ARCHITECTURE & DATA FLOW

**Model:** Monolithic Kernel — single binary loaded at 0x2000, no user/kernel space separation, bare-metal x86.

```
BIOS
 └─ boot/bootloader.asm (Stage 1 MBR @ 0x7C00: load Stage 2 from LBA 1-16)
    └─ boot/boot_stage2.asm (Stage 2 @ 0x7E00: E801, VBE, A20, GDT, kernel load)
       ├─ INT 0x13 LBA read kernel from disk → 0x9000
       ├─ VBE init: INT 0x10 mode 0x115, info at 0x1000
       └─ PM trampoline (forward copy 0x9000 → 0x2000)
          └─ kernel/entry.S (BSS zeroing)
             └─ kernel/kernel.c::kernel_main (C entry)
             ├─ kernel/cpu/gdt.c       → GDT reload
             ├─ kernel/cpu/idt.c       → IDT + PIC remap + exceptions
             ├─ kernel/drivers/screen.c → VGA text / VBE graphics dispatch
             ├─ kernel/drivers/keyboard.c → PS/2 IRQ1
             ├─ kernel/cpu/timer.c     → PIT IRQ0
             ├─ kernel/memory/pfa.c    → page frame allocator
             ├─ kernel/memory/paging.c → identity map 32MB, enable CR0.PG
             ├─ kernel/drivers/graphics.c → VBE framebuffer init + draw
             ├─ kernel/memory/heap.c   → malloc/free/realloc/calloc allocator
             ├─ kernel/acpi/rsdp.c     → RSDP scan (EBDA + BIOS ROM)
             ├─ kernel/acpi/madt.c     → MADT parse → cpu_info[] populated
             ├─ kernel/apic/lapic.c    → LAPIC enable, IPI send
             ├─ kernel/apic/ioapic.c   → I/O APIC IRQ routing
             ├─ kernel/cpu/gdt.c       → per-CPU GDT/TSS/%gs for BSP
             ├─ kernel/drivers/ata.c   → ATA probe
             ├─ kernel/drivers/nvfs.c  → mount NVFS from ATA LBA
             ├─ kernel/cpu/ap_startup.c → start_aps() → INIT+SIPI → APs boot
              ├─ kernel/task.c → preemptive round-robin task mgr (PID, timer IRQ switch)
              ├─ kernel/scheduler/scheduler.c → init work queue
              ├─ kernel/elf.c → exec: per-process PD, ring-3 ELF load, preemptive task switch
              └─ while(1): readline → handle_cmd → dispatch
```

**Disk data flow:** ATA PIO LBA28 → raw sector buffer → NVFS superblock parse → bitmap/inode management → extent-based data read/write. Write (shadow paging): save old extents → alloc new blocks → write data → inode_write (persist) → free old blocks → update bitmap.

**Shell input flow:** PS/2 keyboard IRQ1 (ring buffer) + COM1 serial poll (non-blocking) → `read_char_any()` → `readline()` → line buffer → `handle_cmd()` → dispatch.

## 2. DIRECTORY STRUCTURE

```
Project_002_OS/
├── boot/
│   ├── bootloader.asm        # Stage 1 MBR: loads Stage 2 from LBA 1-16, 512B, w/ partition table
│   ├── boot_stage2.asm       # Stage 2: E801, VBE, A20, GDT, PM trampoline, kernel load (up to 8KB)
│   └── ap_trampoline.asm     # AP startup trampoline (16-bit→PM→paging→ap_main)
├── kernel/
│   ├── entry.S               # Entry: BSS zero → jmp kernel_main
│   ├── kernel.c              # Shell: readline, history, cmd dispatch, serial input, SMP test commands
│   ├── lib.c / lib.h         # Shared utilities: memcpy, memset, strlen, strcpy, strcmp
│   ├── sync/
│   │   └── sync.h            # Spinlocks (irqsave), atomic ops, memory barriers
│   ├── task.c / task.h       # Preemptive round-robin task manager (PID, timer-driven switch)
│   ├── scheduler/
│   │   ├── scheduler.c / .h  # SMP work queue: submit, step, wait_all, 64 slots
│   ├── cpu/
│   │   ├── gdt.c / gdt.h     # Per-CPU GDT (7 entries: null, code, data, ucode, udata, TSS, percpu)
│   │   ├── idt.c / idt.h     # IDT (32 ISR + 16 IRQ), register dump, APIC+pic dual EOI
│   │   ├── interrupt.S       # ISR/IRQ stubs, common handler asm
│   │   ├── ports.h           # inb/outb/inw/outw inline asm
│   │   ├── timer.c / timer.h # PIT channel 0, atomic tick, sleep_ms
│   │   ├── cpu.h             # cpu_info_t, MAX_CPU=8, get_cpu_id via %gs:0
│   │   ├── ap_startup.c / .h # ap_main AP entry, start_aps() SIPI orchestration
│   ├── acpi/
│   │   ├── acpi.h            # RSDP, SDT, RSDT, MADT, Processor, IOAPIC structs
│   │   ├── rsdp.c            # RSDP scan (EBDA 0x40:0x0E + BIOS ROM 0xE0000-0xFFFFF)
│   │   └── madt.c            # MADT parse: count CPUs, populate cpu_info[].apic_id
│   ├── apic/
│   │   ├── lapic.c / lapic.h # LAPIC: enable via MSR 0x1B, EOI, IPI send
│   │   ├── ioapic.c / .h     # I/O APIC: IRQ routing, IMCR programming
│   ├── memory/
│   │   ├── pfa.c / pfa.h     # Page Frame Allocator (bitmap, 32MB)
│   │   ├── paging.c / paging.h # Paging (PD/PT, identity map, CR0.PG)
│   │   ├── heap.c / heap.h   # Heap allocator (malloc/free, boundary tags)
│   ├── elf.c / elf.h         # ELF loader for user-space executables
│   └── drivers/
│       ├── ata.c / ata.h     # ATA PIO: probe, LBA28 read/write
│       ├── nvfs.c / nvfs.h   # NVFS: extent-based filesystem driver
│       ├── keyboard.c / .h     # PS/2 IRQ1: scancode→ASCII, ring buf
│       ├── screen.c / .h     # VGA text + VBE graphics dispatch
│       ├── graphics.c / .h   # VBE framebuffer: pixel, rect, char, scroll
│       ├── font.h            # Generated 8×16 bitmap font (95 chars)
│       └── serial.c / .h    # COM1: init, putchar, puts, puthex, data_available, read_char
├── rootfs/
│   └── triangle.elf          # User-space ELF executable
├── tests/
│   ├── triangle.c             # Test program source
│   ├── app.ld                 # App linker script
│   └── noverix.h              # App header (syscall, VBE info, math)
├── tools/
│   ├── mknvfs.py             # NVFS disk formatter with directory packing (16MB, 32768 sectors)
│   └── genfont.py            # VGA 8×16 bitmap font → font.h generator
├── linker.ld                 # ELF linker: 0x2000, PHDRS RX/RW
├── Makefile                  # clang + nasm + ld.bfd + objcopy + python, ELF + rootfs targets
├── LICENSE                   # GPLv3
├── combine.ps1               # PowerShell disk combination script
├── run.bat                   # Windows batch run script
├── noverix.img               # Combined disk (boot + kernel + NVFS)
├── nvfs_disk.img             # 16MB NVFS raw image
├── README.md
├── CODEBASE_INDEX.md
└── currentfeatures.md
```

## 3. DETAILED MODULE INDEX

---

### `boot/bootloader.asm`

- **Role:** Stage 1 MBR (512 bytes at 0x7C00). Minimal — only loads Stage 2 from LBA 1-16 via LBA (CHS fallback), then jumps to 0x7E00. Includes MBR partition table at offset 446 for VMware/HW BIOS compatibility.
- **Functions:**
  - `start`: Save boot drive, set segments/stack, read Stage 2 via INT 0x13 AH=0x42 (LBA) or AH=0x02 (CHS), far jump to 0x0000:0x7E00 | []
  - `disk_error`: Infinite loop on I/O failure | []
- **Constants:** `STAGE2_SECTORS=16`, `STAGE2_ADDR=0x7E00`, `BOOTDRIVE_ADDR=0x7BFF`
- **Data:** DAP (Disk Address Packet) for LBA read, boot_drive byte
- **Partition table:** 64 bytes at offset 446 — single entry type 0x83 (Linux), LBA start=1, size=max
- **Notes:** No longer handles VBE, E801, A20, GDT, or PM transition — all moved to Stage 2 (boot_stage2.asm). Boot drive passed to Stage 2 via fixed memory address 0x7BFF. The entire Stage 1 fits in < 446 bytes, leaving room for the partition table.

---

### `boot/boot_stage2.asm`

- **Role:** Stage 2 bootloader (loaded at 0x7E00 by Stage 1). Handles all complex boot logic with no 512B limit — kernel disk load, VBE init, E801 memory detection, A20 gate, GDT setup, PM trampoline copy, and transition to 32-bit protected mode.
- **Functions:**
  - `start`: Save boot drive (from 0x7BFF), print banner, read kernel sectors from disk (LBA 17+, LBA with 1-sector loop + segment crossing, CHS fallback) | [print_string, INT 0x13]
  - `print_string`: INT 0x10 teletype output | [BIOS]
  - `disk_error`: Print "Disk!" message and halt | [print_string]
  - `enable_a20`: Port 0x92 + INT 0x15 AX=0x2401 | [I/O ports]
  - `switch_to_pm`: LGDT → enable_a20 → CR0.PE → far jump to PM_TRAMPOLINE_ADDR | [enable_a20]
  - `pm_trampoline` (32-bit): Reload segment regs with DATA_SEG, ESP=0x90000, forward `rep movsd` 0x9000→0x2000, call 0x2000 | []
- **Constants:** `KERNEL_LBA` (1 + STAGE2_SECTORS = 17), `KERNEL_SECTORS` (from kernel.bin size), `KERNEL_LOAD_ADDR=0x9000`, `KERNEL_OFFSET=0x2000`, `BOOTDRIVE_ADDR=0x7BFF`
- **Data:** DAP, boot_drive, message strings ("Noverix Stage 2", "Kernel loaded", "Disk!")
- **Boot flow:**
  1. Print "Noverix Stage 2" banner → read kernel from disk to 0x9000 → print "Kernel loaded"
  2. VBE init: INT 0x10 mode 0x115 (800×600×24bpp LFB), mode info at 0x1000
  3. E801 memory detection: INT 0x15 AX=0xE801, store total RAM at 0x100C
  4. Copy PM trampoline to 0x0500, LGDT, enable A20, CR0.PE
  5. Far jump to PM trampoline at 0x0500
- **Notes:** Forward copy (`cld` `rep movsd`) is safe because dest (0x2000) < src (0x9000). LBA reads 1 sector at a time to avoid SeaBIOS limits (~127 sectors). No 512B constraint — allows verbose debug output and easy maintenance.

---

### `kernel/entry.S`

- **Role:** Kernel entry point (0x2000). Zeroes BSS before calling C code.
- **Functions:**
  - `_start`: Zero BSS (bss_start→bss_end via `rep stosl`) → `jmp kernel_main`
- **Import:** `kernel_main` (C), `bss_start`, `bss_end` (linker.ld)

---

### `kernel/lib.c` + `kernel/lib.h`

- **Role:** Shared utility functions used across kernel modules, eliminating duplicated static implementations.
- **Functions:**
  - `lib_memcpy(dst, src, n)`: Copy n bytes | []
  - `lib_memset(ptr, val, n)`: Set n bytes to val | []
  - `lib_strlen(s)`: String length | []
  - `lib_strcpy(dst, src)`: String copy | [lib_strlen, lib_memcpy]
  - `lib_strcmp(a, b)`: String comparison | []
- **Import:** None (standalone)

---

### `kernel/sync/sync.h`

- **Role:** Spinlocks, atomic operations, and memory barriers for SMP safety.
- **Struct:**
  - `spinlock_t`: {volatile unsigned int locked} — initialized to 0 (unlocked)
- **Functions:**
  - `spin_init(lock)`: Set locked=0 | []
  - `spin_lock(lock)`: While xchg(1) spins | [xchg, barrier]
  - `spin_unlock(lock)`: Store 0 + barrier | [barrier]
  - `spin_lock_irqsave(lock, flags)`: `pushf; cli; spin_lock` | [spin_lock]
  - `spin_unlock_irqrestore(lock, flags)`: `spin_unlock; popf` | [spin_unlock]
  - `spinlock_try_lock(lock)`: `xchg(lock, 1)` → returns 1 if acquired, 0 if already locked (non-blocking) | [atomic_xchg]
  - `atomic_inc(var)`: `lock incl` | []
  - `atomic_dec(var)`: `lock decl` | []
  - `atomic_xchg(ptr, val)`: `xchg` with memory clobber | []
  - `barrier()`: `asm volatile("" ::: "memory")` — compiler barrier | []
- **Lock ordering:** `screen_lock → serial_lock`, `paging_lock → pfa_lock`. No circular dependencies.
- **`spinlock_try_lock`** used by keyboard ISR to avoid deadlock when user task holds `kb_lock` — drops keystrokes under contention instead of deadlocking.
- **Ring-3 deadlock constraint:** Locks acquired at CPL=3 (via `spinlock_lock` without `cli`) are unsafe because IF=1 allows timer preemption. The timer ISR's `task_switch_tick` may switch to another task that tries the same lock via `spinlock_lock_irqsave` (CLI + spin) — deadlock. Solution: all `_user` wrappers are lock-free (see screen.c).

---

### `kernel/kernel.c`

- **Role:** Main shell. Initializes all subsystems, command loop.
- **Functions:**
  - `reboot()`: Send 0xFE to port 0x64 | [inb, outb]
  - `shutdown()`: Write 0x2000 to port 0xB004 + 0x604 | [outw]
   - `history_add(buf)`: Add command to history ring (16 entries), with overflow protection (shifts oldest when full) | [lib_strcpy, lib_strcmp]
   - `readline(buf, max)`: Read input from keyboard + serial (via `read_char_any`), inline editing (LEFT/RIGHT move, mid-line insert/delete), UP/DOWN history, Ctrl+C (char 0x03) cancels line and prints ^C | [read_char_any, print_string, history_add]
   - `read_char_any(void)`: Read char from keyboard (get_char) or serial (serial_read_char) — non-blocking | [get_char, serial_data_available, serial_read_char]
   - `execute_cmd(cmd, arg)`: Dispatch parsed command (extracted from handle_cmd for pipe reuse). Echo redirection has bounds checking on `>` operator. Hex/sleep commands have integer overflow guards. Includes `exec` for ELF binaries, `smp` for parallel task testing, `cpus` for CPU info display | [lib_strcmp, print_string, clear_screen, print_hex, print_int, sleep_ms, nvfs_list, nvfs_chdir, nvfs_read, nvfs_write, nvfs_delete, nvfs_mkdir, nvfs_rmdir, reboot, shutdown, elf_exec, scheduler_submit, scheduler_wait_all]
   - `handle_cmd(buf)`: Parse `|` pipe → split left/right → `set_capture(1)` → `execute_cmd(cmd1, arg1)` → `set_capture(0)` → copy captured output to `pipe_data` → `execute_cmd(cmd2, arg2)` | [execute_cmd, set_capture, get_capture]
   - `kernel_main(void)`: Init sequence → shell loop | [init_serial, init_gdt, init_idt, init_screen, init_keyboard, init_timer, pfa_init, init_paging, heap_init, acpi_init, lapic_init, ioapic_init, gdt_init_percpu(0), ata_init, nvfs_mount, start_aps, scheduler_init]
 - **Static data:** `history[HISTORY_SIZE][LINE_BUF]`, `pipe_data[4096]`, `has_pipe_data`
- **VBE init:** After paging, reads VBE info at `0x1000` (LFB, width, height, pitch, bpp). If LFB is non-zero, maps the framebuffer into page tables via `map_page()` (576 pages for 1024×768×24bpp), then calls `init_graphics()` to activate graphics mode. Falls back to text mode if LFB is zero (VBE unavailable).
- **Import:** `lib.h`, `screen.h`, `keyboard.h`, `serial.h`, `ata.h`, `nvfs.h`, `gdt.h`, `idt.h`, `timer.h`, `ports.h`, `graphics.h`, `elf.h`, `acpi.h`, `cpu.h`, `ap_startup.h`, `scheduler.h`
- **New shell features over FAT16 version:** `mkdir`, `rmdir`, `cd` (with path, `..`, `./..`, `/`). Dynamic prompt showing current path (e.g. `/MYDIR$`). `>>` append operator. `|` pipe operator. `exec` for running ELF executables. `smp`/`cpus` SMP diagnostics. Specific error messages via `nvfs_strerror(nvfs_errno)`.
- **Pipe flow:** `set_capture(1)` → print_*/print_string redirect to 4KB capture buffer → `set_capture(0)` → `get_capture()` → copy to `pipe_data` → set `has_pipe_data=1` → execute cmd2 (cat/echo read pipe_data when arg is empty).
- **Ctrl+C:** When `readline` receives char 0x03, it prints `^C\n` and returns an empty buffer.
- **Serial input:** `readline` polls both keyboard (IRQ1 ring buffer) and COM1 serial (non-blocking poll). Allows command piping via `-serial stdio`.

---

### `kernel/cpu/gdt.c` + `kernel/cpu/gdt.h`

- **Role:** Per-CPU GDT with 7 entries (null, code=0x08, data=0x10, user_code=0x1B, user_data=0x23, TSS, per-CPU data segment). Each CPU has its own GDT with unique TSS and `%gs` base.
- **Struct:** `gdt_entry_t` (8 bytes), `gdt_ptr_t` (packed 6 bytes), `tss_entry_t` (104 bytes)
- **Functions:**
  - `gdt_set_entry(num, base, limit, access, gran)`: Write 8-byte descriptor | []
  - `init_gdt(void)`: Legacy BSP setup — flat code/data segments | []
  - `gdt_init_percpu(cpu_id)`: Create per-CPU GDT: set 7 entries → `lgdt` → reload segment regs + ljmp → load TSS via `ltr` → set `%gs` base via MSR 0xC0000101 | [ports.h, cpu.h]
- **Import:** `ports.h`, `cpu.h`
- **Per-CPU data segment:** GDT entry at index 6 with base = `&cpu_info[cpu_id]`. The `%gs` segment is loaded with this base, allowing `get_cpu_id()` to read the first field of `cpu_info_t` via `%gs:0`.

---

### `kernel/cpu/idt.c` + `kernel/cpu/idt.h`

- **Role:** IDT with 256 entries (32 exception + 16 IRQ). PIC remap. Exception handler with full register dump.
- **Struct/Type:**
  - `idt_entry_t`, `idt_ptr_t` (packed)
  - `registers_t`: {gs, fs, es, ds, edi, esi, ebp, esp, ebx, edx, ecx, eax, int_no, err_code, eip, cs, eflags, useresp, ss}
  - `interrupt_handler_t`: function pointer `void (*)(registers_t*)`
- **Functions:**
  - `idt_set_entry(num, base, sel, flags)`: Write IDT entry | []
  - `dump_registers(regs)`: Print all regs to screen + serial | [print_hex, print_string, serial_write_hex, serial_write_string]
  - `exception_handler(regs)`: Clear screen → dump → halt | [dump_registers]
  - `isr_handler(regs)`: Dispatch to handler or `exception_handler` | []
  - `irq_handler(regs)`: Send EOI(s) → handler. Sends LAPIC EOI + PIC EOI when `apic_enabled` | [outb, lapic_eoi]
  - `irq_remap(void)`: PIC master+slave remap (ICW1-ICW4) | [outb]
  - `register_interrupt_handler(irq_no, handler)`: Atomic pushf/cli/popf → set handler | []
  - `init_idt(void)`: Set 256 entries → LIDT → irq_remap → STI | [idt_set_entry, irq_remap]
  - `idt_install(void)`: Reload IDT for APs (LIDT only) | []
- **Import:** `ports.h`, `screen.h`, `serial.h`, `lapic.h`, 32 extern ISR labels + 16 extern IRQ labels (interrupt.S)
- **Dual EOI:** After APIC init, `irq_handler` sends both `lapic_eoi()` and PIC EOI (outb 0x20/0xA0) because ISA interrupts arrive via PIC → LAPIC LINT0 (ExtINT mode).

---

### `kernel/cpu/interrupt.S`

- **Role:** ISR/IRQ stubs via GAS macros. Push interrupt number + error code, call C handler.
- **Macros:**
  - `ISR_NOERR num`: push 0 + push num → jmp isr_common
  - `ISR_ERR num`: push num → jmp isr_common
  - `IRQ num vec`: push 0 + push vec → jmp irq_common
- **Labels:**
  - `isr_common`: pusha → push segment regs → call `isr_handler` → pop → iret
  - `irq_common`: same pattern, calls `irq_handler`
- **Imports:** 32 ISR + 16 IRQ global labels → call `isr_handler`/`irq_handler` (idt.c)

---

### `kernel/cpu/ports.h`

- **Role:** Inline I/O port helpers.
- **Inline functions:**
  - `inb(port)`: read 1 byte | []
  - `outb(port, data)`: write 1 byte | []
  - `inw(port)`: read 1 word | []
  - `outw(port, data)`: write 1 word | []

---

### `kernel/cpu/cpu.h`

- **Role:** CPU information structure and `get_cpu_id()` macro for SMP.
- **Struct:**
  - `cpu_info_t`: {apic_id (uint), state (volatile uint), stack_top (uint), rsvd[4]} — first field is apic_id, read via `%gs:0`
- **Constants:** `MAX_CPU=8`, `CPU_STATE_DOWN=0`, `CPU_STATE_READY=1`
- **Global:** `cpu_info[MAX_CPU]` — per-CPU data array, indexed by logical CPU ID
- **Inline:**
  - `get_cpu_id(void)`: `unsigned int id; __asm__("mov %%gs:0, %0" : "=r"(id)); return id;` — reads `cpu_info[cpu_id].apic_id` via `%gs:0`
- **Per-CPU segment:** GDT entry 6 has base = `&cpu_info[cpu_id]`. `%gs` loaded with this segment per CPU.

---

### `kernel/cpu/timer.c` + `kernel/cpu/timer.h`

- **Role:** PIT channel 0 tick counter. `sleep_ms` function. Provides time source for file timestamps.
- **Functions:**
  - `timer_handler(regs)`: Increment `tick_count` by 1 | []
  - `init_timer(freq)`: Set divisor, register IRQ0 handler | [outb, register_interrupt_handler]
  - `get_ticks(void)`: Return tick_count | []
  - `sleep_ms(ms)`: Busy-wait based on tick difference. Uses `pause` instead of `hlt` when APs are running (BSP may need to avoid deadlock) | []
- **Import:** `idt.h`, `ports.h`

---

### `kernel/cpu/ap_startup.c` + `kernel/cpu/ap_startup.h`

- **Role:** AP startup — trampoline, SIPI orchestration, `ap_main()` entry for application processors.
- **Functions:**
  - `start_aps(void)`: For each discovered CPU (BSP excluded): init per-CPU GDT → set `page_dir_phys` + `ap_main_addr` in trampoline data area (0x70200) → INIT assert (10ms) → INIT de-assert (10ms) → SIPI vector 0x70 (200μs) → SIPI again (200μs). Spins waiting for each AP to set state=READY (timeout ~2s) | [lapic_send_ipi, gdt_init_percpu, sleep_ms]
  - `ap_main(unsigned int apic_id)`: Called by trampoline. Reads APIC ID from `cpu_info[]`, sets `cpu_id = index`, calls `gdt_init_percpu(cpu_id)`, `idt_install()`, `init_timer()`, allocates per-CPU stack (4KB from PFA), sets `cpu_info[cpu_id].state = CPU_STATE_READY`, enters idle loop calling `scheduler_step()` | [gdt_init_percpu, idt_install, init_timer, alloc_frame, scheduler_step, get_cpu_id]
- **Import:** `lapic.h`, `gdt.h`, `idt.h`, `pfa.h`, `cpu.h`, `scheduler.h`, `timer.h`
- **SIPI protocol:** INIT level-assert blocks LAPIC timer delivery — `sleep_ms` during INIT uses spin loops instead of timer wait. AP trampoline binary embedded via `ld -r -b binary`.

---

### `kernel/boot/ap_trampoline.asm`

- **Role:** AP startup trampoline (16-bit real mode at 0x70000 → protected mode with paging → ap_main).
- **Data area (0x70200):** `page_dir_phys` (4B), `ap_main_addr` (4B) — written by BSP before SIPI.
- **Flow:** `org 0x70000`, set 16-bit segments → `lgdt` (trampoline GDT) → enable A20 (port 0x92) → set CR0.PE → ljmp to 32-bit → reload segments → set CR3 + CR0.PG (paging) → load `ap_main_addr` → call.
- **Trampoline GDT:** 3 entries (null, code=0x08, data=0x10) — minimal flat GDT for transition, replaced by per-CPU GDT inside ap_main.

---

### `kernel/memory/pfa.c` + `pfa.h`

- **Role:** Page Frame Allocator — bitmap-based physical memory manager.
- **Static data:** `bitmap[1024]` (8192 bits for 32MB, 1 bit per 4KB frame)
- **Functions:**
  - `pfa_init(void)`: Mark reserved frames (null page, kernel, stack, legacy 0xA0000-0xFFFFF) via loop starting at 0x00000000 | [set_bit, serial_write_string]
  - `alloc_frame(void)`: Scan bitmap → first 0 bit → set to 1 → return physical address | [test_bit, set_bit]
  - `alloc_frames(count)`: Allocate count contiguous frames (first-fit scan) | [test_bit, set_bit]
  - `free_frame(addr)`: Clear corresponding bit | [clear_bit]
  - `free_frames(addr, count)`: Clear bits for count contiguous frames | [clear_bit]
  - `get_free_frame_count(void)`: Return number of free frames | [test_bit]
  - `set_bit(frame)`: Internal — set bit in bitmap | []
  - `clear_bit(frame)`: Internal — clear bit | []
  - `test_bit(frame)`: Internal — test bit | []
  - `mark_frame(addr)`: Internal — set bit for frame containing addr | [set_bit]
- **Import:** `serial.h`, `bss_end` (linker symbol)

---

### `kernel/acpi/acpi.h`

- **Role:** ACPI data structures for RSDP, SDT header, RSDT, MADT, Processor Local APIC, I/O APIC structs.
- **Structs:**
  - `rsdp_t`: {signature[8], checksum, oem[6], revision, rsdt_addr (uint)} — 20 bytes, revision=0 for ACPI v1
  - `sdt_header_t`: {signature[4], length, revision, checksum, oem[6], oem_table[8], oem_rev, creator_id, creator_rev}
  - `rsdt_t`: {header (sdt_header_t), entries[]} — array of uint table pointers
  - `madt_t`: {header (sdt_header_t), lapic_addr (uint), flags (uint)} — followed by type-length-value entries
  - `madt_entry_processor_t`: {type=0, length=6, acpi_id, apic_id, flags} — flags bit 0 = "enabled"
  - `madt_entry_ioapic_t`: {type=1, length=12, id, addr (uint), gsi_base (uint)}

---

### `kernel/acpi/rsdp.c`

- **Role:** Find RSDP (Root System Description Pointer) via EBDA or BIOS ROM scan.
- **Functions:**
  - `acpi_init(void)`: Scan EBDA address at `0x40:0x0E` → scan BIOS ROM (0xE0000–0xFFFFF) → find "RSD PTR " signature → validate checksum → return `rsdp_t*` | [lib_memcmp]
  - `acpi_rsdp(void)`: Entry point — return found RSDP pointer or 0 | []
- **Import:** `acpi.h`, `lib.h`, `serial.h`, `paging.h`
- **Identity-mapping:** ACPI tables may reside above 32MB. Each table is identity-mapped via `map_page()` before reading.

---

### `kernel/acpi/madt.c`

- **Role:** Parse MADT (Multiple APIC Description Table) to discover CPUs and I/O APIC.
- **Global:** `cpu_count`, `cpu_info[MAX_CPU]` — populated with APIC IDs of enabled processors
- **Functions:**
  - `madt_parse(rsdp)`: Walk RSDT entries → find "APIC" signature → parse MADT body → for each type-0 (processor) entry with flags bit 0 set, assign `cpu_info[cpu_count++].apic_id` → identity-map I/O APIC address | [lib_memcmp, map_page]
- **Import:** `acpi.h`, `cpu.h`, `serial.h`, `paging.h`

---

### `kernel/apic/lapic.c` + `kernel/apic/lapic.h`

- **Role:** Local APIC driver — enable, EOI, IPI send, AP startup.
- **Constants:** `LAPIC_BASE=0xFEE00000`, `LAPIC_MSR=0x1B`, `LAPIC_ENABLE=0x800`, `IA32_APIC_BASE=0x1B`
- **Registers (MMIO offsets from LAPIC_BASE):**
  - `LAPIC_ID=0x20`, `LAPIC_SVR=0xF0`, `LAPIC_EOI=0xB0`, `LAPIC_ICR_LOW=0x300`, `LAPIC_ICR_HIGH=0x310`, `LAPIC_LVT_TIMER=0x320`, `LAPIC_LVT_LINT0=0x350`, `LAPIC_LVT_LINT1=0x360`, `LAPIC_LVT_ERROR=0x370`, `LAPIC_SPURIOUS=0xF0`
- **Functions:**
  - `lapic_init(void)`: Map LAPIC page → read LAPIC ID → set SVR=0x1FF (spurious vec 0xFF, enable) → set LVT_LINT0=0x700 (ExtINT, not masked) → set LVT_LINT1=0x10000 (masked) → set LVT_ERROR=0x10000 (masked) → set LVT_TIMER=0x10000 (masked) → `apic_enabled=1` | [map_page, serial_write_string]
  - `lapic_eoi(void)`: Write 0 to LAPIC_EOI | [mmio_write]
  - `lapic_send_ipi(apic_id, vector, shorthand)`: Write ICR high (APIC ID) → ICR low (vector + trigger + delivery) → poll ICR bit 12 (delivery status) | [mmio_read, mmio_write]
  - `apic_is_enabled(void)`: Return `apic_enabled` flag | []
- **Import:** `paging.h`, `cpu.h`, `serial.h`
- **LINT0 ExtINT:** QEMU routes ISA interrupts (PIT, keyboard) through PIC → LAPIC LINT0 in virtual wire mode. LINT0 must be configured as ExtINT (0x700) and PIC must stay unmasked.

---

### `kernel/apic/ioapic.c` + `kernel/apic/ioapic.h`

- **Role:** I/O APIC driver — IRQ routing, IMCR (Interrupt Mode Control Register) programming.
- **Constants:** `IOAPIC_BASE=0xFEC00000`, `IOAPIC_REG_INDEX=0x00`, `IOAPIC_REG_DATA=0x10`, `IOAPIC_ID=0x00`, `IOAPIC_VER=0x01`, `IOAPIC_REDIR_TBL=0x10`
- **Functions:**
  - `ioapic_init(void)`: Map I/O APIC page → read version → write IMCR (port 0x22/0x23, value 0x01 = PIC + APIC both receive) → route IRQ0→vector 0x20 (PIT, CPU 0) → route IRQ1→vector 0x21 (keyboard, CPU 0) → mask all other redirections | [map_page, outb, ioapic_write_redir]
  - `ioapic_read_reg(reg)`: Select index → read data | []
  - `ioapic_write_reg(reg, val)`: Write data | []
  - `ioapic_write_redir(irq, vector, dest, flags)`: Set redirection entry for IRQ (two 32-bit writes) | [ioapic_write_reg]
- **Import:** `paging.h`, `ports.h`, `serial.h`
- **IRQ routing:** Only IRQ0 and IRQ1 are routed (to CPU 0). All other IRQs masked. APs receive no I/O APIC interrupts.

---

### `kernel/memory/paging.c` + `paging.h`

- **Role:** 32-bit x86 two-level paging. Identity map 0-32MB, enable CR0.PG.
- **Static data:** `page_dir` (1024 PDEs, allocated from PFA)
- **Functions:**
  - `init_paging(void)`: Alloc PD + first PT + PTs 4-32MB → load CR3 → set CR0.PG | [alloc_frame, create_table]
  - `create_table(virt, flags)`: Internal — alloc page table, set PDE | [alloc_frame]
  - `map_page(virt, phys, flags)`: Map virtual → physical, create PT if needed, invlpg | [create_table]
  - `unmap_page(virt)`: Remove page mapping, clear PTE, flush TLB | []
  - `get_page_mapping(virt)`: Walk PD+PT, return physical page if present | []
  - `dump_page_info(virt)`: Debug — serial-log PDE/PTE details for a virtual address | [serial_write_string, serial_write_hex, get_page_mapping]
  - `read_cr0(void)`: Return CR0 | []
  - `read_cr3(void)`: Return CR3 | []
- **Import:** `pfa.h`, `serial.h`
- **User-space paging:** `paging_enable_user_access()` globally sets `PAGE_USER` on all existing page table entries. `page_dir_add_user_flag()` modifies the page directory to add `PAGE_USER` to every PDE/PTE. Used during ELF init to allow user-mode access to kernel-mapped pages (heap, screen buffers, framebuffer).

---

### `kernel/memory/heap.c` + `heap.h`

- **Role:** Kernel heap allocator — boundary-tag first-fit malloc/free/realloc/calloc.
- **Constants:** `HEAP_START=0x800000`, `HEAP_SIZE=0x200000` (2MB region, identity mapped)
- **Functions:**
  - `heap_init(void)`: Create 1 free block covering entire HEAP_SIZE | [set_footer, serial_write_string]
  - `malloc(size)`: Walk blocks → first-fit → split if remainder >= MIN_BLOCK (12) → return ptr after header | [set_footer]
  - `calloc(num, size)`: Zeroed allocation — malloc + memset | [malloc, lib_memset]
  - `realloc(ptr, size)`: Resize allocation — shrink in-place (split) or alloc-copy-free | [malloc, free, lib_memcpy]
  - `free(ptr)`: Mark free → merge with next block (if free) → merge with prev block via boundary tag footer (with bounds guard) | [set_footer]
  - `heap_walk(void)`: Debug — iterate all blocks, print address/type/size, totals | [serial_write_string, serial_write_hex, serial_write_int]
  - `set_footer(addr, size)`: Write size at block end (for boundary tag) | []
- **Import:** `serial.h`
- **Bug fix:** Removed `prev_addr = (unsigned int)prev_hdr;` in backward merge (`free()`). This line assigned `prev_addr` the address of a local stack pointer instead of the heap block address, causing wrong footer and heap corruption.
- **User-safe wrappers:** `malloc_user()`, `free_user()`, `realloc_user()` — identical to kernel versions but use `spinlock_lock`/`spinlock_unlock` (no `cli`) to avoid GPF when called from ring 3.

---

### `kernel/elf.c` + `elf.h`

- **Role:** ELF loader — loads 32-bit ELF executables from NVFS and creates ring-3 user tasks with per-process page directories.
- **Constants:** `ELF_MAGIC=0x464C457F`, `PT_LOAD=1`, `SYSCALL_INT=0x80`, `USER_CS=0x1B`, `USER_DS=0x23`, `USER_STACK_ADDR=0x009FF000`, `USER_ENTRY_MIN=0x00800000`, `USER_ENTRY_MAX=0x00A00000`, `USER_PDE_IDX=2`
- **Functions:**
  - `elf_exec(path)`: Read ELF from NVFS → validate magic/headers → per-segment bounds checks (p_offset+p_filesz overflow, p_filesz≤p_memsz) → allocate per-process page directory via `page_dir_create()` → clear PDE[2] for fresh page tables → allocate 512 physical frames for 0x800000–0xA00000 and map via `map_page_to_dir()` → switch to task PD → `lib_memcpy` segments → copy `kernel_api` struct → set up user stack with API pointer + exit trampoline → switch back to kernel PD → create task via `alloc_task()` with ring-3 IRET frame (CS=0x1B, SS=0x23, DS/ES/FS/GS=0x23, EFLAGS=0x200, EIP=entry) → add to ready list | [nvfs_read, alloc_frame, page_dir_create, map_page_to_dir, serial_write_string, alloc_task]
  - `syscall_handler(regs)`: Handle interrupt 0x80 — dispatch SYS_EXIT (cleanup via `free_task_resources` which walks per-process PTEs/PTs/PD, scheduler switch with `page_dir_switch`) | []
  - `free_task_resources(t)`: Free per-process page tables (skip PDEs matching kernel), their mapped physical pages, the PD frame, and the kernel stack | []
  - `kernel_api` struct: Function pointer table exposing ~30 user-safe entry points — wrappers use **no locks** (see screen.c rationale). Lock-free serial helpers avoid `cli` at CPL=3.
- **Import:** `nvfs.h`, `pfa.h`, `paging.h`, `idt.h`, `serial.h`, `task.h`
- **User-space execution:** ELF segments mapped into 0x00800000–0x00A00000 region via `map_page_to_dir()`. Separate physical frames per task (not shared with kernel heap). IRET frame sets CS=0x1B (user code, RPL=3), SS=DS=ES=FS=GS=0x23 (user data, RPL=3), EFLAGS=0x200 (IF=1). User task created as preemptive PID via `alloc_task()`.
- **API calls:** User code invokes kernel functions via direct function-call through a pointer table. No syscall overhead for most operations — only `SYS_EXIT` uses `int 0x80` because it requires privilege escalation to clean up task state.
- **Segment selectors:** CS=0x1B (GDT index 3 with RPL=3), SS/DS/ES/FS/GS=0x23 (GDT index 4 with RPL=3). These match the GDT entries set up by `gdt_init_percpu()`.
- **Security:** ELF header validation (magic, phentsize ≥ sizeof(phdr), e_entry bounds 0x00800000–0x00A00000). Per-segment bounds checks with integer overflow guards (`p_offset + p_filesz` overflow, `p_filesz > p_memsz` reject). All segment loads must stay within the allowed range. Rejects malformed/invalid ELFs with serial-logged errors. Memory isolation via per-process page directories + ring 3 segments. `PAGE_USER` set on all accessible pages. No `cli`-based locks at CPL=3 (all API wrappers are lock-free).

---

### `kernel/drivers/ata.c` + `ata.h`

- **Role:** ATA PIO driver — probe primary/secondary master/slave, LBA28 read/write.
- **Static data:** `ata_exists[2][2]`, `ata_model[2][2][41]`, `ata_padding[4096]` (BSS overflow workaround)
- **Functions:**
  - `ata_init(void)`: Probe all 4 devices (ch=0..1, dr=0..1): outb device select → outb IDENTIFY (0xEC) → poll BSY→ data → extract model string | [inb, outb, inw]
  - `ata_drive_exists(ch, dr)`: Check `ata_exists[ch][dr]` | []
  - `ata_get_model(ch, dr)`: Return model string | [ata_drive_exists]
  - `ata_pio(ch, dr, lba, count, buffer, write)`: LBA28 read (0x20) or write (0x30). Poll BSY+DRQ, sector-by-sector | [inb, outb, outw, inw, ata_drive_exists]
  - `ata_read_sectors(ch, dr, lba, count, buffer)`: Call `ata_pio(write=0)` | [ata_pio]
  - `ata_write_sectors(ch, dr, lba, count, buffer)`: Call `ata_pio(write=1)` | [ata_pio]
- **Import:** `ports.h`, `serial.h`
- **Model string safety:** Model string is extracted from IDENTIFY data via a bounded loop (40 bytes) into `ata_model[2][2][41]`, ensuring no overflow into adjacent BSS. Stack buffer `buf[256]` holds raw IDENTIFY data from port reads, avoiding BSS corruption entirely.

---

### `kernel/drivers/nvfs.c` + `nvfs.h`

- **Role:** NVFS (Noverix File System) — extent-based filesystem driver replacing FAT16.
- **Constants:** `NVFS_MAGIC="NVFS"`, `NVFS_SECTOR_SIZE=512`, `NVFS_INODE_SIZE=128`, `NVFS_DIRENT_SIZE=32`, `NVFS_MAX_EXTENTS=14` (on-disk struct), `NVFS_DIRECT_EXTENTS=13`, `NVFS_INDIRECT_ENTS=64`, `NVFS_INDIRECT_MARKER=0xFFFFFFFF`, `NVFS_MAX_NAME=27`, `NVFS_ROOT_INODE=0`
- **Error codes:** `NVFS_ERR_NOT_FOUND=1`, `NVFS_ERR_NOT_DIR=2`, `NVFS_ERR_NOT_FILE=3`, `NVFS_ERR_DIR_BUSY=4`, `NVFS_ERR_NO_SPACE=5`, `NVFS_ERR_NO_INODE=6`, `NVFS_ERR_EXISTS=7`, `NVFS_ERR_IO=8`, `NVFS_ERR_NO_MOUNT=9`, `NVFS_ERR_PATH=10`
- **Global:** `nvfs_errno` — set by all public API functions on error
- **Data types:**
  - `nvfs_extent`: {start (uint), count (uint)} — a contiguous extent
  - `nvfs_inode`: {size (uint), type (byte), ctime[3] (24-bit), extent_count (uint), extents[14], mtime (uint)}
    - type: `NVFS_TYPE_FILE=1`, `NVFS_TYPE_DIR=2`
    - ctime: creation time (seconds since boot, 24-bit ~194 day range)
    - mtime: modification time (32-bit seconds since boot)
  - `nvfs_dirent`: {name[28], inode (uint)} — directory entry
- **Static data:** `mounted`, `nvfs_ch`, `nvfs_dr`, `nvfs_cwd`, `sb_*` (superblock fields incl. `sb_inode_blocks`)
- **Disk format:**
  - Superblock (sector 1): magic "NVFS" + all uint fields + `inode_blocks` (offset 36) + state byte + padding
  - Block bitmap (sectors 2-9): 4096 bytes = 32768 bits for data blocks
  - Inode table (sectors 10-41): initial 128 inodes × 128 bytes = 32 sectors, expandable
  - Data blocks (sectors 42-32767): 32726 blocks × 512 bytes = ~16MB
- **Internal functions (static):**
  - `read_sector(lba, buf)` / `write_sector(lba, buf)`: Single sector I/O | [ata_read_sectors, ata_write_sectors]
  - `read_block(block, buf)` / `write_block(block, buf)`: Map block → LBA (data_start + block) | [read_sector, write_sector]
  - `bitmap_test(block)`, `bitmap_set(block, used)`, `bitmap_find(count)`: Block bitmap management | [read_sector, write_sector]
  - `inode_read(inum, inode)` / `inode_write(inum, inode)`: Read/write inode (includes ctime[3] and mtime fields) | [read_sector, write_sector]
  - `now_sec(void)`: Return seconds since boot (`get_ticks() / 100`) | [get_ticks]
  - `inode_set_ctime(inode, t)`: Pack 24-bit timestamp into ctime[3] | []
  - `inode_alloc(type)`: Find free inode — if exhausted, calls `expand_inode_table()` | [inode_read, inode_write, expand_inode_table]
  - `inode_free(inum)`: Free inode + all extent blocks (including indirect) | [inode_read, inode_write, bitmap_set, extent_load_all]
  - `expand_inode_table(void)`: Alloc block from bitmap → zero → extend inode table → update superblock | [bitmap_find, bitmap_set, sb_write_field]
  - `sb_write_field(offset, val)`: Write uint to superblock | [read_sector, write_sector]
  - `indirect_read_extents(lba, exts, max)`: Read extents from indirect block | [read_block]
  - `indirect_write_extents(lba, exts, count)`: Write extents to indirect block | [write_block]
  - `indirect_alloc(void)`: Allocate + zero an indirect block | [bitmap_find, write_block, bitmap_set]
  - `extent_load_all(inode, out, max)`: Load all extents (direct + indirect) into flat array | [indirect_read_extents]
  - `extent_read(inode, buf, max)`: Read file content from extents (via extent_load_all) | [read_block]
  - `extent_write(inode, data, size)`: Write file content — alloc contiguous blocks, create 1 extent | [bitmap_find, write_block, bitmap_set]
  - `dir_find(parent_inum, name)`: Find entry in directory (via extent_load_all) | [inode_read, read_block]
  - `dir_add_extent(inode, block)`: Add new extent to directory — if 13 direct full, create indirect block | [indirect_alloc, indirect_read_extents, indirect_write_extents]
  - `dir_add(parent_inum, name, child_inum)`: Add entry, find empty slot or alloc block + extent | [inode_read, inode_write, read_block, write_block, bitmap_alloc, dir_add_extent]
  - `dir_remove(parent_inum, name)`: Remove entry | [inode_read, read_block, write_block]
  - `dir_empty(dir_inum)`: Check if directory is empty | [inode_read, read_block]
  - `to_upper(s)`: uppercase | []
  - `resolve_path(path, *parent_inode, name)`: Parse path — `/`, `..`, `.`, `./..` | [dir_find, find_parent]
  - `find_parent(inum)`: Scan all inodes to find parent directory | [inode_read, read_block, extent_load_all]

- **Public API:**
  - `nvfs_mount(void)`: Iterate all 4 ATA positions (ch 0-1, dr 0-1) at offsets 0 and 2880 → read superblock → set sb_* fields on first "NVFS" signature found | [ata_drive_exists, read_sector]
  - `nvfs_list(path)`: List directory — `[DIR]` tag, decimal size | [resolve_path, inode_read, read_block, extent_load_all]
  - `nvfs_read(path, buf, max)`: Read file | [resolve_path, dir_find, inode_read, extent_read]
  - `nvfs_write(path, data, size)`: Write/overwrite file with shadow paging (alloc new → persist inode → free old) | [resolve_path, dir_find, inode_alloc, extent_write, dir_add, inode_write]
  - `nvfs_delete(path)`: Delete file | [resolve_path, dir_find, inode_read, inode_free, dir_remove]
  - `nvfs_mkdir(path)`: Create directory | [resolve_path, inode_alloc, dir_add]
  - `nvfs_rmdir(path)`: Remove empty directory | [resolve_path, dir_find, inode_read, dir_empty, inode_free, dir_remove]
  - `nvfs_chdir(path, *out_inode)`: Change directory | [resolve_path, dir_find]
  - `nvfs_get_cwd(void)`: Return cwd inode | []
  - `nvfs_path_string(inum, buf, size)`: Build path string (e.g. `/MYDIR/SUBDIR`) | [find_parent, inode_read, read_block, extent_load_all]
  - `nvfs_is_mounted(void)`: Check mount state | []
  - `nvfs_append(path, data, size)`: Append to file with shadow paging | [resolve_path, dir_find, inode_read, extent_read, extent_write, inode_write]
  - `nvfs_strerror(err)`: Return error description string | []

- **Import:** `nvfs.h`, `ata.h`, `screen.h`, `serial.h`
- **Linked Extents:** When `extent_count > NVFS_DIRECT_EXTENTS (13)`, extent[13] points to an indirect block containing up to 64 additional extents. `extent_load_all()` reads all into a cache array.
- **Shadow Paging:** Write order: alloc new blocks → write data → inode_write → free old blocks. Power loss at any point leaves old data intact.
- **Dynamic Inode Table:** When `inode_alloc()` runs out of inodes, `expand_inode_table()` allocates a new block from the bitmap and updates the superblock.

---

### `kernel/drivers/keyboard.c` + `keyboard.h`

- **Role:** PS/2 keyboard driver. IRQ1 handler reads scancode → ASCII → ring buffer.
- **Static data:** `key_buffer[256]` (ring), `shift_pressed`, `caps_on`, `extended` flag, `typematic_param`
- **Tables:** `scancode_ascii[58]`, `scancode_ascii_shift[58]`
- **Functions:**
  - `keyboard_handler(regs)`: Read scancode → handle extended (0xE0), shift, caps → push ASCII/keycode to ring buffer | [inb, outb]
  - `get_char(void)`: Get 1 char from ring buffer (non-blocking) | []
  - `read_char(void)`: Blocking get_char | [get_char]
  - `init_keyboard(void)`: Reset state, register IRQ1 handler, set typematic to 30Hz/250ms | [register_interrupt_handler, keyboard_set_typematic]
  - `keyboard_set_typematic(param)`: Send PS/2 command 0xF3 to configure repeat rate/delay | [inb, outb]
  - `keyboard_get_typematic(void)`: Return current typematic parameter byte | []
- **Import:** `ports.h`, `idt.h`
- **User-safe wrappers:** `get_char_user()`, `read_char_user()` — use `spinlock_lock`/`spinlock_unlock` (no `cli`) instead of `spin_lock_irqsave`/`spin_unlock_irqrestore` to avoid GPF at CPL=3.
- **ISR deadlock avoidance:** `keyboard_handler` uses `spinlock_try_lock` instead of `spin_lock_irqsave` — if user task holds `kb_lock` (e.g., blocked in `read_char_user`), the ISR drops the keystroke instead of deadlocking.

---

### `kernel/drivers/screen.c` + `screen.h`

- **Role:** VGA text mode (80×25) + VBE graphics mode (1024×768) dispatch driver. Character output, hex/dec display, scroll, cursor. Capture mode for shell pipe operator.
- **Static data:** `capture_mode`, `capture_buf[4096]`, `capture_pos` (with bounds protection)
- **VBE Dispatch:** `clear_screen`, `set_cursor`, `print_char`, `scroll` check `is_graphics_active()` and call the VBE framebuffer versions when active:
  - `clear_screen` → `fill_rect(..., GFX_BG)`
  - `set_cursor` → no-op (hardware cursor unused)
  - `print_char` → `draw_char_gfx()` for printable chars
  - `scroll` → `scroll_gfx()`
  - Text mode versions used when `is_graphics_active()` returns 0.
- **Functions:**
  - `clear_screen(void)`: Fill VGA memory (0xB8000) or framebuffer with spaces/black | [set_cursor, fill_rect]
  - `init_screen(void)`: Call clear_screen | [clear_screen]
  - `set_cursor(x, y)`: port 0x3D4/0x3D5 in text mode, no-op in graphics | [outb]
  - `scroll(void)`: Copy rows in text mode, or `scroll_gfx()` in graphics | []
  - `print_char(c)`: Print 1 char at cursor, handle \n, \b, \t. In capture mode stores to `capture_buf`. In graphics mode calls `draw_char_gfx()` | [scroll, set_cursor, draw_char_gfx]
  - `print_string(str)`: Print to serial (COM1) then screen. Skips serial in capture mode | [serial_write_string, print_char]
  - `print_hex(num)`: Print 8-digit hex (0x + 8 nibbles) | [print_string]
  - `print_int(num)`: Print decimal number | [print_char]
  - `set_capture(on)`: Enable/disable capture mode, reset `capture_pos` on enable, null-terminate on disable | []
  - `get_capture(void)`: Null-terminate and return pointer to captured output | []
- **Import:** `ports.h`, `serial.h`, `graphics.h`, `font.h`
- **User-safe wrappers:** `clear_screen_user()`, `print_string_user()`, `print_char_user()`, `print_hex_user()`, `print_int_user()`, `set_cursor_user()` — **no locks, no serial output**. At CPL=3, IF=1 means a timer ISR can preempt while a lock is held. If the ISR's `task_switch_tick` switches to another task that tries the same lock via `spinlock_lock_irqsave` (CLI + spin), the system deadlocks (spinner never yields, lock-holder never runs). Lock-free serial helpers (`serial_write_string_user`, etc.) write directly to COM1 port I/O without synchronization. Per-pixel framebuffer writes and cursor_x/y access tolerate concurrent access (best-effort accuracy).

---

### `kernel/drivers/serial.c` + `serial.h`

- **Role:** COM1 serial port driver — early boot logging + shell input.
- **Functions:**
  - `init_serial(void)`: Configure COM1 (baud rate, 8N1, FIFO, DTR+RTS) | [outb]
  - `is_transmit_empty(void)`: Check line status bit 5 | [inb]
  - `serial_write_char(c)`: Wait TX empty → outb | [is_transmit_empty, outb]
  - `serial_write_string(str)`: While loop | [serial_write_char]
  - `serial_write_hex(num)`: 8-digit hex string (0x + 8 nibbles) | [serial_write_string]
  - `serial_data_available(void)`: Check line status bit 0 (data ready) | [inb]
  - `serial_read_char(void)`: Read 1 byte from serial (non-blocking) | [inb]
- **Import:** `ports.h`

---

### `kernel/drivers/graphics.c` + `graphics.h`

- **Role:** VBE framebuffer driver — pixel-level drawing, bitmap font rendering, scrolling.
- **Static data:** `lfb_ptr` (framebuffer address), `fb_active` (flag), `fb_width/height/pitch/bpp/bpp_bytes`
- **Functions:**
  - `init_graphics(lfb, width, height, pitch, bpp)`: Set LFB pointer and dimensions, activate graphics mode | []
  - `is_graphics_active(void)`: Return `fb_active` flag | []
  - `draw_pixel(x, y, color)`: Set pixel at (x,y) to 24/32-bit color | [pixel_write]
  - `fill_rect(x, y, w, h, color)`: Fill rectangle with solid color | [pixel_write]
  - `draw_char_gfx(x, y, c, fg, bg)`: Render 8×16 bitmap char at pixel position | [char_row, pixel_write]
  - `scroll_gfx(lines)`: Scroll framebuffer up by N lines (font rows) | [fill_rect]
  - `fb_cols(void)`: Return `fb_width` (pixel columns, e.g. 800) | []
  - `fb_rows(void)`: Return `fb_height` (pixel rows, e.g. 600) | []
- **Internal:**
  - `pixel_write(ptr, color)`: Write 24/32-bit color to framebuffer address | []
  - `char_row(c, row)`: Look up font bitmap row for character | []
- **Import:** `graphics.h`, `font.h`
- **Pixel format:** 24bpp (3 bytes: B, G, R) or 32bpp (4 bytes: B, G, R, A). `pixel_write` handles both based on `fb_bpp_bytes`.

---

### `kernel/drivers/font.h`

- **Role:** Generated 8×16 bitmap font for framebuffer rendering.
- **Data:** `font_data[95][16]` — `static const unsigned char` array, one 16-byte column per ASCII char 32–126 (space through tilde). Each byte represents 8 pixels horizontally (MSB = leftmost pixel).
- **Constants:** `FONT_WIDTH=8`, `FONT_HEIGHT=16`, `FONT_FIRST_CHAR=32`, `FONT_LAST_CHAR=126`, `FONT_NUM_CHARS=95`
- **Generation:** Created by `tools/genfont.py` by extracting VGA ROM font from any system's VGA BIOS (reads from `/dev/fb0` or uses built-in VGA ROM data).
- **Note:** Each `.c` file that includes this header gets its own copy (static). Only `graphics.c` includes it.

---

### `tools/genfont.py`

- **Role:** Python script to generate `kernel/drivers/font.h` with VGA 8×16 bitmap font data.
- **Functions:**
  - `get_vga_font()`: Try to read VGA ROM font from `/sys/devices/virtual/...` or Linux console, fall back to hardcoded VGA ROM data.
  - Emit `font_data[95][16]` C array as `static const unsigned char`.
- **Output:** `kernel/drivers/font.h`
- **Usage:** `python3 tools/genfont.py`
- **Note:** Only needs to be re-run if the font changes. The generated file is committed.

---

### `kernel/task.c` + `task.h`

- **Role:** Preemptive round-robin task manager (PID-based). Manages kernel and user tasks with timer-driven preemptive switching.
- **Constants:** `TASK_FREE=0`, `TASK_READY=1`, `TASK_RUNNING=2`, `TASK_BLOCKED=3`, `TASK_ZOMBIE=4`, `TASK_STACK_SIZE=0x1000`
- **Struct:**
  - `task_t`: {pid (uint), state (volatile int), cpu_assigned (int), page_dir (page_dir_t), kernel_esp (uint), kernel_stack_base (void*), next (task_t*)} — circular linked list
- **Global:** `ready_head` (circular list root), `sched_lock` (spinlock), `next_pid` counter
- **Functions:**
  - `task_init(void)`: Init sched_lock, clear ready_head, set next_pid=1, set current_task=0 | [spinlock_init]
  - `alloc_task(void)`: Allocate frame from PFA → zero → assign next_pid++ → return task_t* (used by elf.c for user tasks) | [alloc_frame]
  - `task_create(entry)`: Create kernel task: allocate stack → set up IRET frame (CS=0x08, EFLAGS=0x202) → set kernel_esp → set page_dir = kernel → add to ready list | [alloc_task, alloc_frames, spinlock_lock]
  - `task_switch_tick(current_esp)`: Timer-tick entry: save CPU state → pick_next_locked() → switch page_dir → load new ESP. Called from timer ISR. Returns new task's kernel_esp or 0 for no switch | [spinlock_lock, pick_next_locked, gdt_set_kernel_stack, page_dir_switch]
  - `task_idle_loop(void)`: AP idle loop — call scheduler_step(), pause if no work | [scheduler_step]
  - `pick_next_locked()`: Internal — scan circular ready list for TASK_READY tasks not assigned to another CPU | []
- **Import:** `pfa.h`, `paging.h`, `gdt.h`, `timer.h`, `cpu.h`, `scheduler.h`, `serial.h`
- **Preemptive switching:** Timer ISR calls `task_switch_tick()` every ~10ms (100Hz). If `task_switch_pending` is set (by elf_exec), performs round-robin switch. Saves current kernel_esp, searches for next READY task, switches kernel stack + page directory.
- **User task switching:** When switching to a user task, `task_switch_tick` returns the kernel_esp of the new task. The interrupt return path (IRET) pops the user-mode frame (CS=0x1B, EIP=user_entry) from the new stack, entering ring 3 transparently.
- **`page_dir_switch(next->page_dir)` is unconditional** — the old `if (next->page_dir != kernel_page_dir)` guard skipped the CR3 reload when returning to the kernel task, leaving the user PD active and causing heap corruption (kernel heap at 0x800000–0xA00000 mapped to user's physical pages). Always switching ensures the correct page directory is active.
- **Circular ready list:** Tasks form a singly-linked circular list. `pick_next_locked` starts from current and wraps around. Multiple CPUs can own separate tasks — `cpu_assigned` prevents duplicate scheduling.

### `kernel/scheduler/scheduler.c` + `kernel/scheduler/scheduler.h`

- **Role:** SMP work queue scheduler — pull-based model for parallel task execution across CPUs.
- **Constants:** `SCHEDULER_MAX_TASKS=64`
- **Struct:**
  - `sched_task_t`: {func (void (*)(void*)), arg (void*), state (volatile int), cpu_id (volatile int)}
  - States: `SCHED_FREE=0`, `SCHED_SUBMITTED=1`, `SCHED_RUNNING=2`
- **Static data:** `tasks[SCHEDULER_MAX_TASKS]`, `sched_lock` (spinlock)
- **Functions:**
  - `scheduler_init(void)`: Init sched_lock, clear all task slots | [spin_init]
  - `scheduler_submit(func, arg)`: Find free slot → set func+arg → state = SUBMITTED | [spin_lock_irqsave, spin_unlock_irqrestore]
  - `scheduler_step(void)`: Scan for SUBMITTED → CAS to RUNNING → execute → state = FREE. Returns 1 if ran, 0 if nothing to do | [atomic_xchg]
  - `scheduler_wait_all(void)`: Spin (calling scheduler_step to also process tasks) until all submitted tasks are FREE. BSP participates in work execution during wait | [scheduler_step]
- **Import:** `sync.h`
- **Usage:** BSP submits tasks, any idle CPU picks them. `smp` shell command submits 4 sum tasks. AP idle loop calls `scheduler_step()` in a `pause` loop.

---

### `tests/triangle.c`

- **Role:** Ring-3 user-space Sierpinski triangle demo. Compiled as ELF executable loaded by `elf_exec`.
- **Flow:** `main(void *arg)`: set DS/ES/FS/GS=0x23 (user data) → `is_graphics_active()` check → `clear_screen()` → chaos game: pick random vertex, draw midpoint pixel (50000 iterations with 10ms sleep) → "done" message → `exit()`.
- **API interface:** Receives `noverix_api_t` function pointer table via `arg` (ESR from ELF loader). Calls: `clear_screen`, `print_string`, `draw_pixel`, `sleep_ms`, `get_ticks`, `fb_cols`, `fb_rows`, `read_char`, `exit`. No syscall overhead — direct function calls.
- **Segment init:** `mov %0, %%ds` etc. inline asm at entry to set DS/ES/FS/GS=0x23 (user data segment with RPL=3). Required because x86 does not automatically restore segment registers on IRET to ring 3.
- **Build:** Compiled `-m32 -ffreestanding -fno-pie -fno-pic`, linked with `tests/app.ld` at load address 0x00800000. Output: `rootfs/triangle.elf`.

### `tests/noverix.h`

- **Role:** User-space API header. Defines `noverix_api_t` struct — function pointer table matching `kernel_api` in `elf.c`.
- **Fields:** ~30 function pointers covering screen (clear, print, cursor), keyboard (get_char, read_char), graphics (draw_pixel, fill_rect, fb_cols, fb_rows), timer (sleep_ms, get_ticks), heap (malloc, free), NVFS (read, write, delete, mkdir, rmdir, chdir, list, strerror), and exit.
- **Layout must match** `kernel_api` struct in `elf.c` exactly — same field order and pointer size.

### `tests/app.ld`

- **Role:** Linker script for user-space ELF executables.
- **Layout:** Text, data, BSS sections starting at `0x00800000` (8 MB — above kernel heap at 0x800000–0xA00000).
- **Entry:** `main` — called by user-space startup with `noverix_api_t*` in argument register.
