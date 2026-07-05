# Noverix

A minimal x86 hobby operating system built from scratch. Boots from real mode into 32-bit protected mode with paging, heap allocator, and a command-line shell. Uses **NVFS** (Noverix File System) — an extent-based filesystem that replaces FAT16.

## Features

| Area | Details |
|------|---------|
| **Boot** | Real-mode → protected-mode transition, A20 gate enable, GDT loading, forward `rep movsd` safe kernel copy. |
| **Disk I/O** | ATA PIO (LBA28) read/write, dual-channel primary/secondary, IDENTIFY-based drive detection, sector-level access. |
| **NVFS** | Extent-based filesystem: superblock (sector 1), block bitmap (sectors 2–9), inode table (sectors 10–41, expandable), data blocks (sectors 42–32767). Each inode has 13 direct extents + 1 indirect block pointer (linked extents, up to 64 more extents). Shadow paging for crash-safe writes. Dynamic inode table expansion when full. File timestamps (ctime/mtime, seconds since boot) stored in inode. |
| **VGA text mode** | 80×25 text buffer, hardware cursor, terminal scrolling, hex/dec rendering. Fallback when VBE unavailable. |
| **VBE graphics mode** | 800×600×24bpp framebuffer (mode 0x115), bitmap font rendering (8×16), pixel-level draw, smooth scrolling. Automatic dispatch in screen driver when active. |
| **PS/2 keyboard** | Interrupt-driven ring buffer, shift/caps, command history with arrow keys, configurable typematic rate (PS/2 command 0xF3, `rate` shell command). |
| **Interrupts** | IDT with 32 exception ISRs and 16 IRQs. Full register dump on exception (`ud2` crash command). |
| **Timer (PIT)** | Tick counter via IRQ0, `sleep_ms()` for delays, `get_ticks()` for time source. |
| **Serial I/O** | COM1 serial port — kernel logging, debug output, and shell input via `-serial mon:stdio`. |
| **Memory** | Page Frame Allocator (bitmap-based, 1 bit per 4KB frame, 8192 frames for 32MB). |
| **Paging** | 32-bit x86 two-level paging (PD + PT), identity map 0–32MB, `map_page()`/`unmap_page()`/`get_page_mapping()` for custom mappings, CR0.PG enabled. |
| **Heap** | `malloc`/`free`/`realloc`/`calloc` allocator at 0x800000 (2MB), boundary-tag first-fit, split/coalesce, serial OOM logging, `heap_walk()` debug dump. |
| **PFA** | Bitmap-based page frame allocator, single-frame + contiguous multi-frame allocation (`alloc_frames`/`free_frames`), `get_free_frame_count()` query. |
| **Spinlocks** | SMP-safe spinlocks with `irqsave`/`irqrestore` for all shared modules (PFA, heap, paging, screen, serial, keyboard). Lock ordering enforced. |
| **Shared library** | `lib.c/h` provides `memcpy`/`memset`/`strlen`/`strcpy`/`strcmp` used across all kernel modules, eliminating code duplication. |
| **ACPI** | RSDP scan (EBDA + BIOS ROM), RSDT walk, MADT parse. APIC IDs discovered for all CPUs. Identity-maps tables above 32MB. |
| **APIC** | LAPIC enabled via MSR 0x1B, mapped at 0xFEE00000. I/O APIC at 0xFEC00000 routes IRQ0/IRQ1. IPI (INIT+SIPI) for AP startup. Spurious vector 0xFF. LINT0 ExtINT mode for legacy PIC passthrough. |
| **SMP** | Per-CPU GDT (7 entries: null, code, data, ucode, udata, TSS, percpu) with `%gs` base for `get_cpu_id()`. AP trampoline at 0x70000. INIT+SIPI protocol. APs enter idle loop with `pause`. Up to 8 CPUs. |
| **Scheduler** | Pull-based SMP work queue: `scheduler_submit()`/`scheduler_step()`/`scheduler_wait_all()`. State machine (free→submitted→running→free). Tasks execute on any idle CPU. `smp` shell command for parallel testing. |
| **ELF loader** | Loads and executes ELF binaries from NVFS into user-space, basic syscall interface via software interrupt. |
| **Ring-3 user mode** | User tasks run at CPL=3 via IRET with correct segment selectors (CS=0x1B, SS=0x23, DS/ES/FS/GS=0x23). Kernel API exposed via function pointer table (clear_screen, print_string, draw_pixel, malloc/free, NVFS, etc.). User-safe wrappers use `spinlock_lock` (no `cli`) to avoid GPF. Timer-driven preemptive scheduler switches between kernel (PID 1) and user tasks. |
| **Shell** | Command history (UP/DOWN), inline editing (LEFT/RIGHT/Backspace), Ctrl+C to cancel line, path navigation with `cd`, `cd ..`, `cd ./..`, `ls <path>`, `cat`, `echo > file`, `echo >> file` (append), `|` pipe (e.g. `cmd1 | cmd2`), `exec` for running ELF executables, `mkdir`, `rmdir`, `rm`, `heap`/`mem`/`pages` debug commands, `smp`/`cpus` SMP diagnostics. Specific error messages (e.g. "Not found", "Directory not empty") instead of generic "FAIL". Dynamic prompt shows current directory path (e.g. `/MYDIR$`). |

## Requirements

### Mandatory

| Tool | Purpose |
|------|---------|
| **NASM** | Assembles the bootloader to flat binary |
| **Clang** (with `-m32` support) | Compiles kernel C and assembly to 32-bit x86 code |
| **GNU ld.bfd** | Links ELF kernel image |
| **GNU Make** | Orchestrates the build |
| **QEMU** (system-x86_64) | Emulates the x86 machine |
| **Python 3** | Runs `tools/mknvfs.py` to create NVFS disk image |

### Installing dependencies

**Linux (Debian/Ubuntu):**

```sh
sudo apt install build-essential clang nasm qemu-system-x86 python3
```

## Build

```sh
make
```

Produces `build/os-image.bin` — a 1.44 MB floppy image and `nvfs_disk.img` — a 16 MB NVFS disk.

### Make targets

| Target | Action |
| --- | --- |
| `make` / `make all` | Build `os-image.bin` + `nvfs_disk.img` |
| `make clean` | Remove `build/`, images |
| `make run-qemu` | Floppy + NVFS disk (QEMU, `-vga std -smp 2 -serial mon:stdio`) |
| `make run-qemu-nrx` | Single-disk HDD boot with NVFS (`-vga std -smp 2 -serial mon:stdio`) |
| `make iso` | Build `build/os-image.iso` |
| `make noverix.img` | Combined disk: `dd` to USB for real boot |

## Run

```sh
make run-qemu           # floppy + separate NVFS (default)
make run-qemu-nrx       # single disk HDD boot
```

Or directly:

**Floppy + NVFS disk:**
```sh
qemu-system-x86_64 -boot order=a \
  -drive format=raw,file=build/os-image.bin,if=floppy \
  -drive file=nvfs_disk.img,format=raw,if=none,id=ata0 \
  -device ide-hd,drive=ata0 -m 32 -smp 2 -serial mon:stdio
```

**Single-disk HDD (combined):**
```sh
qemu-system-x86_64 -boot order=c \
  -drive file=noverix.img,format=raw,if=none,id=ata0 \
  -device ide-hd,drive=ata0 -m 32 -smp 2 -serial mon:stdio
```

**Real hardware (USB):**
```sh
sudo dd if=noverix.img of=/dev/sdX bs=512
```

- `nvfs_disk.img` is a 16 MB raw image formatted as NVFS.
- `noverix.img` is a combined disk (bootloader + kernel + NVFS) for single-device boot.
- All file operations persist across reboots.
- Use `-serial mon:stdio` for serial console output with QEMU monitor access via Ctrl-A, C.

## Shell

```
Noverix v0.1
================
Type 'help' for commands.

/$ help
Noverix Shell
----------------
clear    Clear screen
echo     Print text or write file (echo text > file, echo text >> file for append)
cat      Display file contents (cat reads pipe when no file)
ls       List files with mtime (ls, ls <dir>)
cd       Change directory
mkdir    Create directory
rmdir    Remove empty directory
rm       Delete file
hex      Print a number in hex
ver      Show version
sleep    Sleep for N milliseconds
ata      List ATA drives
crash    Trigger a crash (for testing)
reboot   Reboot system
shutdown Power off
```

### Commands reference

| Command | Syntax | Description |
| --- | --- | --- |
| `help`, `?` | `help` | Show available commands |
| `echo` | `echo <text>` | Print text to screen |
| `echo` (write) | `echo text > file` | Write text to a file (creates or overwrites) |
| `echo` (append) | `echo text >> file` | Append text to an existing file |
| pipe | `cmd1 \| cmd2` | Pipe cmd1 output to cmd2 (cat/echo read pipe when no arg/file) |
| `cat` | `cat <file>` | Display file contents (no file arg → reads from pipe) |
| `ls` | `ls [path]` | List directory contents — decimal file sizes, `[DIR]` prefix + `<DIR>` for directories |
| `cd` | `cd [path]` | Change directory. Supports `/` (root), `..`, `./..`, `.` |
| `mkdir` | `mkdir <path>` | Create directory |
| `rmdir` | `rmdir <path>` | Remove empty directory |
| `rm` | `rm <file>` | Delete a file |
| `clear` | `clear` | Clear screen and reset cursor |
| `hex` | `hex <num>` | Parse decimal number and print in hex |
| `ver` | `ver` | Print OS version |
| `sleep` | `sleep <ms>` | Sleep for N milliseconds (1–10000) |
| `ata` | `ata` | List detected ATA drives with model strings |
| `crash` | `crash` | Trigger `ud2` exception with register dump |
| `reboot` | `reboot` | Reset system via keyboard controller (port 0x64, 0xFE) |
| `shutdown`, `poweroff` | `shutdown` | Power off via ACPI ports (0xB004, 0x604) |
| `exec` | `exec <file>` | Load and run an ELF executable from NVFS |
| `heap` | `heap` | Dump heap allocator block state |
| `mem` | `mem` | Show physical memory usage (total/used/free frames) |
| `pages`    | `pages`    | Show page directory/table mappings |
| `rate`     | `rate [delay rate]` | Show or set keyboard repeat rate (delay:0=250ms..3=1000ms, rate:0=fast..31=slow) |

## Project layout

```
.
├── boot/
│   ├── bootloader.asm          # MBR bootloader (real-mode → PMode trampoline)
│   └── ap_trampoline.asm       # AP startup trampoline (16-bit→PM→paging→ap_main)
├── kernel/
│   ├── entry.S                 # Kernel entry (_start), BSS zeroing
│   ├── kernel.c                # Shell loop, command parsing, readline + SMP test commands
│   ├── lib.c / lib.h           # memcpy, memset, strlen, strcpy, strcmp
│   ├── elf.c / elf.h           # ELF loader for user-space executables
│   ├── sync/
│   │   └── sync.h              # Spinlocks, atomic ops, memory barriers
│   ├── scheduler/
│   │   ├── scheduler.c / .h    # SMP work queue (pull-based, 64 slots)
│   ├── cpu/
│   │   ├── gdt.c / gdt.h       # Per-CPU GDT with TSS + per-CPU data segment
│   │   ├── idt.c / idt.h       # IDT entries, exception handler, register dump, APIC EOI
│   │   ├── interrupt.S         # ISR/IRQ stubs (GAS macros)
│   │   ├── ports.h             # inb/outb/inw/outw inline asm
│   │   ├── timer.c / timer.h   # PIT driver, sleep_ms
│   │   ├── cpu.h               # cpu_info_t, MAX_CPU, get_cpu_id via %gs:0
│   │   ├── ap_startup.c / .h   # AP entry (ap_main), SIPI orchestration (start_aps)
│   ├── acpi/
│   │   ├── acpi.h              # RSDP, SDT, RSDT, MADT, Processor, IOAPIC structs
│   │   ├── rsdp.c              # RSDP scan (EBDA + BIOS ROM)
│   │   └── madt.c              # MADT parse, CPU discovery
│   ├── apic/
│   │   ├── lapic.c / .h        # LAPIC init, EOI, IPI send
│   │   └── ioapic.c / .h       # I/O APIC IRQ routing, IMCR
│   ├── memory/
│   │   ├── pfa.c / pfa.h       # Page Frame Allocator (bitmap, 32MB)
│   │   ├── paging.c / paging.h # Paging (PD/PT, identity map, CR0.PG)
│   │   ├── heap.c / heap.h     # Heap allocator (malloc/free, boundary tags)
│   └── drivers/
│       ├── ata.c / ata.h             # ATA PIO driver (LBA28, IDENTIFY)
│       ├── nvfs.c / nvfs.h            # NVFS extent-based filesystem driver
│       ├── keyboard.c / keyboard.h    # PS/2 keyboard driver
│       ├── screen.c / screen.h        # VGA text + VBE graphics dispatch
│       ├── graphics.c / graphics.h    # VBE framebuffer: pixel, rect, char, scroll
│       ├── font.h                     # Generated bitmap font 8×16 (95 chars)
│       └── serial.c / serial.h        # COM1 serial driver
├── build/                      # Build artifacts
├── tools/
│   ├── mknvfs.py               # NVFS disk formatter with directory packing (16MB)
│   ├── genfont.py              # VGA 8×16 bitmap font → font.h generator
├── rootfs/                     # User-space ELF executables packed into NVFS
│   └── triangle.elf            # Triangle test program
├── tests/
│   ├── triangle.c              # Triangle test source
│   ├── app.ld                  # App linker script
│   └── noverix.h               # App header
├── LICENSE                     # GPLv3
├── noverix.img                 # Combined disk (boot + kernel + NVFS)
├── nvfs_disk.img               # 16 MB NVFS disk image
├── linker.ld                   # ELF linker script
├── Makefile                    # Build system
├── README.md
└── CODEBASE_INDEX.md
```

## Memory map

| Address | Size | Contents |
| --- | --- | --- |
| `0x0500` | ~256 B | PM trampoline (32-bit) |
| `0x0600` | 256 B | VBE mode info buffer (during boot) |
| `0x1000` | 11 B | VBE info: LFB (4B) + width (2B) + height (2B) + pitch (2B) + bpp (1B) |
| `0x2000` | variable | Kernel destination (executed from here) |
| `0x70000` | ~1 KB | AP trampoline: 16-bit → PM → paging → ap_main (data at 0x70200) |
| `0x7C00` | 512 B | Bootloader MBR and 16-bit stack |
| `0x90000` | variable | 32-bit stack (grows downward) |
| `0xA0000` | 384 KB | Legacy/BIOS/VGA (reserved) |
| `0xB8000` | 4 KB | VGA text framebuffer (80×25 × 2 bytes, unused in VBE mode) |
| `0x100000` | ~31 MB | Free physical memory (managed by PFA + identity mapped) |
| `0xFEC00000` | 4 KB | I/O APIC (identity-mapped) |
| `0xFEE00000` | 4 KB | LAPIC (identity-mapped) |
| `0xFD000000` | ~3 MB | VBE LFB framebuffer (identity-mapped via `map_page()` at kernel init) |

## Boot process

```
BIOS → 0x7C00 (bootloader MBR)
  ├── Real-mode setup, INT 0x13 loads kernel sectors to 0x9000
  ├── A20 gate enable (port 0x92)
  ├── VBE init: INT 0x10 mode 0x118 (1024×768×24bpp LFB), info stored at 0x1000
  ├── GDT load, CR0 bit 0 set
  └── Far jump to PM trampoline at 0x0500

PM Trampoline (32-bit)
  ├── Reload segment registers with DATA_SEG (0x10)
  ├── Set ESP = 0x90000
  ├── Forward rep movsd: copy kernel 0x9000 → 0x2000
  └── Call 0x2000 (_start)

_start → kernel_main
  ├── entry.S: BSS zeroing (bss_start → bss_end)
  ├── init_serial() — early debug logging
  ├── init_gdt() / init_idt() — protected mode + interrupts
  ├── init_screen() / init_keyboard() / init_timer()
  ├── pfa_init() — page frame allocator (bitmap, mark reserved frames)
  ├── init_paging() — identity map 32MB, enable CR0.PG
  ├── VBE init: read info at 0x1000 → map LFB into page tables → init_graphics()
  ├── heap_init() — malloc/free at 0x800000 (2MB)
  ├── acpi_init() — RSDP scan → RSDT → MADT → discover CPUs
  ├── lapic_init() / ioapic_init() — enable LAPIC, route IRQs
  ├── gdt_init_percpu(0) — per-CPU GDT/TSS/%gs for BSP
  ├── ata_init() — probe ATA drives
  ├── nvfs_mount() — mount NVFS volume
  ├── start_aps() — INIT+SIPI → APs boot, load per-CPU GDT, enter idle
  ├── scheduler_init() — init SMP work queue
  └── Shell loop
```

## Key notes

- **No standard library** — kernel is `-ffreestanding -nostdlib`. Custom `strlen`/`strcpy`/`strcmp`.
- **BSS zeroing** in entry.S prevents crashes when static variables extend past loaded sectors.
- **Paging** uses identity mapping (virt = phys) for first 32MB. Page directory at dynamically allocated physical frame. `map_page()` supports custom non-identity mappings.
- **Heap** at 0x800000–0xA00000 (2MB, identity mapped). Boundary tag allocator: each block has a 4-byte header (size + LSB alloc flag) and 4-byte footer for O(1) backward merge. Minimum allocation 12 bytes.
- **ATA on secondary channel** — QEMU's `-device ide-hd` attaches to channel 1. NVFS driver probes all channels.
- **NVFS extent-based design** — each inode holds 13 direct extents + 1 indirect block pointer (linked extents: up to 64 more extents stored in an indirect sector). Files are read/written in a single sequential pass per extent — no cluster chain walking. Simpler and faster than FAT.
- **Shadow paging** — writes use the pattern: alloc new blocks → write data → persist inode → free old blocks. If power fails mid-write, old data remains intact. No journaling overhead.
- **Dynamic inode table** — when the 128 inodes are exhausted, the driver allocates a new block from the bitmap, expands the inode table, and updates the superblock. No more fixed inode limit.
- **NVFS disk format** — 16 MB (32768 sectors). Bitmap covers data blocks only. Root inode is always 0. Directory entries are 32 bytes (name[28] + inode[4]), 16 per sector.
- **Serial input** — shell accepts input from both keyboard and COM1 serial port. Use `-serial mon:stdio` for headless operation and scripting.
- **VBE Graphics** — bootloader sets VBE mode 0x118 (1024×768×24bpp LFB) via INT 0x10, stores mode info at `0x1000` (LFB, width, height, pitch, bpp). Kernel reads this info, maps the LFB into page tables via `map_page()` (576 pages for 3MB framebuffer), and calls `init_graphics()` from `graphics.c`. The screen driver (`screen.c`) dispatches `print_char`/`clear_screen`/`scroll` to either VGA text (0xB8000) or VBE framebuffer based on `is_graphics_active()`. Font is a generated 8×16 bitmap at `kernel/drivers/font.h` from `tools/genfont.py`. Use QEMU's `-vga std` to enable.
- **Security hardening** — bounds checking on shell command parser (echo redirection), history buffer overflow protection, integer overflow guards in hex/sleep commands, heap underflow guard in free(), frame 0 marking fix in PFA, capture buffer bounds protection in screen driver, atomic interrupt handler registration (pushf/popf), `-Werror` in CFLAGS.
- **SMP startup** — BSP discovers CPUs via ACPI MADT, starts APs via INIT+SIPI IPI protocol. AP trampoline at 0x70000 transitions from real mode through protected mode with paging to calling `ap_main()`. Each CPU has its own GDT (7 entries: null, code, data, ucode, udata, TSS, percpu) and `%gs` points to its `cpu_info_t` for `get_cpu_id()`.
- **Spinlocks** — all shared modules (PFA, heap, paging, screen, serial, keyboard) use `irqsave` spinlocks. Lock ordering: `screen_lock → serial_lock`, `paging_lock → pfa_lock`. No circular dependencies.
- **IRQ routing on SMP** — QEMU routes ISA interrupts through PIC → LAPIC LINT0 (virtual wire mode), not through I/O APIC redirection entries. LINT0 must be ExtINT mode and PIC must stay unmasked for PIT/keyboard to work. All other I/O APIC redirections are masked.
- **SMP scheduler** — pull-based work queue (not preemptive). BSP submits tasks via `scheduler_submit()`, idle CPUs pick them via `scheduler_step()`. AP idle loop uses `pause` (not `hlt`) because I/O APIC only delivers IRQ0/IRQ1 to CPU 0.
- **Ring-3 user mode** — ELF loader creates ring-3 tasks via IRET with correct segment selectors. Kernel API (screen, keyboard, heap, NVFS, graphics) exposed via function pointer table. All API functions use `spinlock_lock` wrappers (no `cli`) to avoid GPF at CPL=3. Keyboard ISR uses `spinlock_try_lock` to avoid deadlock with user-held locks. VBE framebuffer mapped with `PAGE_USER`. Timer-driven preemptive round-robin switches between shell (PID 1) and user tasks (PID 2+).
- **Bootloader HDD boot** — reads 1 sector at a time via LBA (`int 0x13, ah=0x42`) to avoid SeaBIOS limits on DAP sector count (>127). Falls back to CHS if extended read unsupported.
