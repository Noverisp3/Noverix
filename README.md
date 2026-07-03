# Noveris OS

A minimal x86 hobby operating system built from scratch. Boots from real mode into 32-bit protected mode with paging, heap allocator, and a command-line shell. Uses **NVFS** (Noveris File System) — an extent-based filesystem that replaces FAT16.

## Features

| Area | Details |
|------|---------|
| **Boot** | Real-mode → protected-mode transition, A20 gate enable, GDT loading, forward `rep movsd` safe kernel copy. |
| **Disk I/O** | ATA PIO (LBA28) read/write, dual-channel primary/secondary, IDENTIFY-based drive detection, sector-level access. |
| **NVFS** | Extent-based filesystem: superblock (sector 1), block bitmap (sectors 2–9), inode table (sectors 10–41, 128 inodes), data blocks (sectors 42–32767). Each inode stores up to 14 extents (start + count) for sequential reads — no cluster chain walking. |
| **VGA text mode** | 80×25 text buffer, hardware cursor, terminal scrolling, hex/dec rendering. |
| **PS/2 keyboard** | Interrupt-driven ring buffer, shift/caps, command history with arrow keys. |
| **Interrupts** | IDT with 32 exception ISRs and 16 IRQs. Full register dump on exception (`ud2` crash command). |
| **Timer (PIT)** | Atomic tick counter via `LOCK XADD`, `sleep_ms()` for delays. |
| **Serial I/O** | COM1 serial port — kernel logging, debug output, and shell input via `-serial stdio`. |
| **Memory** | Page Frame Allocator (bitmap-based, 1 bit per 4KB frame, 8192 frames for 32MB). |
| **Paging** | 32-bit x86 two-level paging (PD + PT), identity map 0–32MB, `map_page()` for custom mappings, CR0.PG enabled. |
| **Heap** | `malloc`/`free` allocator at 0x800000 (2MB), boundary-tag first-fit, split/coalesce, serial OOM logging. |
| **Shell** | Command history (UP/DOWN), inline editing (LEFT/RIGHT/Backspace), path navigation with `cd`, `cd ..`, `cd ./..`, `ls <path>`, `cat`, `echo > file`, `mkdir`, `rmdir`, `rm`. Dynamic prompt shows current directory path (e.g. `/MYDIR$`). |

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
| `make clean` | Remove `build/` and `nvfs_disk.img` |
| `make run-qemu` | Build and launch QEMU with both images |

## Run

```sh
make run-qemu
```

Or directly:

```sh
qemu-system-x86_64 -boot order=a \
  -drive format=raw,file=build/os-image.bin,if=floppy \
  -drive file=nvfs_disk.img,format=raw,if=none,id=ata0 \
  -device ide-hd,drive=ata0 -m 32 -serial stdio
```

- Add `-display gtk` for graphical window (omit for serial-only console output).
- `nvfs_disk.img` is a 16 MB raw image formatted as NVFS.
- All file operations persist across reboots.
- Use `-serial stdio` to pipe shell commands via script.

## Shell

```
Noveris OS v0.1
================
Type 'help' for commands.

/$ help
Noveris OS Shell
----------------
clear    Clear screen
echo     Print text or write file (echo text > file)
cat      Display file contents
ls       List files/directories
cd       Change directory
mkdir    Create directory
rmdir    Remove directory
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
| `cat` | `cat <file>` | Display file contents |
| `ls` | `ls [path]` | List directory contents — shows `[DIR]` prefix + `<DIR>` size for directories |
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

## Project layout

```
.
├── boot/
│   └── bootloader.asm          # MBR bootloader (real-mode → PMode trampoline)
├── kernel/
│   ├── entry.S                 # Kernel entry (_start), BSS zeroing
│   ├── kernel.c                # Shell loop, command parsing, readline
│   ├── cpu/
│   │   ├── gdt.c/h             # GDT entries
│   │   ├── idt.c/h             # IDT entries, exception handler, register dump
│   │   ├── interrupt.S         # ISR/IRQ stubs (GAS macros)
│   │   ├── ports.h             # inb/outb/inw/outw inline asm
│   │   ├── timer.c/h           # PIT driver
│   ├── memory/
│   │   ├── pfa.c/h             # Page Frame Allocator (bitmap, 32MB)
│   │   ├── paging.c/h          # Paging (PD/PT, identity map, CR0.PG)
│   │   ├── heap.c/h            # Heap allocator (malloc/free, boundary tags)
│   └── drivers/
│       ├── ata.c/h             # ATA PIO driver (LBA28, IDENTIFY)
│       ├── nvfs.c/h            # NVFS extent-based filesystem driver
│       ├── keyboard.c/h        # PS/2 keyboard driver
│       ├── screen.c/h          # VGA text mode driver
│       ├── serial.c/h          # COM1 serial driver
├── build/                      # Build artifacts
├── tools/
│   └── mknvfs.py               # NVFS disk formatter (16MB, 128 inodes)
├── nvfs_disk.img               # 16 MB NVFS disk image
├── linker.ld                   # ELF linker script
├── Makefile                    # Build system
└── README.md
```

## Memory map

| Address | Size | Contents |
| --- | --- | --- |
| `0x0500` | ~256 B | PM trampoline (32-bit) |
| `0x0600` | variable | Kernel staging buffer (loaded from disk) |
| `0x2000` | variable | Kernel destination (executed from here) |
| `0x7C00` | 512 B | Bootloader MBR and 16-bit stack |
| `0x90000` | variable | 32-bit stack (grows downward) |
| `0xA0000` | 384 KB | Legacy/BIOS/VGA (reserved) |
| `0xB8000` | 4 KB | VGA text framebuffer (80×25 × 2 bytes) |
| `0x100000` | ~31 MB | Free physical memory (managed by PFA + identity mapped) |

## Boot process

```
BIOS → 0x7C00 (bootloader MBR)
  ├── Real-mode setup, INT 0x13 loads kernel sectors to 0x9000
  ├── A20 gate enable (port 0x92)
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
  ├── heap_init() — malloc/free at 0x800000 (2MB)
  ├── ata_init() — probe ATA drives
  ├── nvfs_mount() — mount NVFS volume
  └── Shell loop
```

## Key notes

- **No standard library** — kernel is `-ffreestanding -nostdlib`. Custom `strlen`/`strcpy`/`strcmp`.
- **BSS zeroing** in entry.S prevents crashes when static variables extend past loaded sectors.
- **Paging** uses identity mapping (virt = phys) for first 32MB. Page directory at dynamically allocated physical frame. `map_page()` supports custom non-identity mappings.
- **Heap** at 0x800000–0xA00000 (2MB, identity mapped). Boundary tag allocator: each block has a 4-byte header (size + LSB alloc flag) and 4-byte footer for O(1) backward merge. Minimum allocation 12 bytes.
- **ATA on secondary channel** — QEMU's `-device ide-hd` attaches to channel 1. NVFS driver probes all channels.
- **NVFS extent-based design** — each inode holds up to 14 extents (start block + block count). Files are read/written in a single sequential pass per extent — no cluster chain walking. This is simpler and faster than FAT.
- **NVFS disk format** — 16 MB (32768 sectors). Bitmap covers data blocks only. Root inode is always 0. Directory entries are 32 bytes (name[28] + inode[4]), 16 per sector.
- **Serial input** — shell accepts input from both keyboard and COM1 serial port. Use `-serial stdio` for headless operation and scripting.
