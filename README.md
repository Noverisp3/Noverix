# Noveris OS

A minimal x86 hobby operating system built from scratch. Boots from real mode into 32-bit protected mode and runs a command-line shell with ATA PIO disk driver and FAT16 filesystem support.

## Features

| Area | Details |
|------|---------|
| **Boot** | Real-mode → protected-mode transition, A20 gate enable, GDT loading, safe kernel copy via backwards `rep movsd`. |
| **Disk I/O** | ATA PIO (LBA28) read/write, dual-channel primary/secondary, IDENTIFY-based drive detection, sector-level access. |
| **FAT16** | BPB parsing, root directory listing, 8.3 filename lookup, cluster chain traversal, file read/write/delete, cluster allocation and freeing. |
| **VGA text mode** | 80×25 text buffer, hardware cursor, terminal scrolling, hex/dec rendering. |
| **PS/2 keyboard** | Interrupt-driven ring buffer, shift/caps, command history with arrow keys. |
| **Interrupts** | IDT with 32 exception ISRs and 16 IRQs. Full register dump on exception (`ud2` crash command). |
| **Timer (PIT)** | Atomic tick counter via `LOCK XADD`, `sleep_ms()` for delays. |
| **Serial I/O** | COM1 serial port driver for kernel logging and debugging. |
| **Memory** | Page Frame Allocator (bitmap-based, 1 bit per 4KB frame, 8192 frames for 32MB). |
| **Paging** | 32-bit x86 two-level paging (PD + PT), identity map 0–32MB, `map_page()` for custom mappings, CR0.PG enabled. |
| **Heap** | `malloc`/`free` allocator at 0x800000 (2MB), boundary-tag first-fit, split/coalesce, serial OOM logging. |
| **Shell** | Command history (UP/DOWN), inline editing (LEFT/RIGHT/Backspace), `help`, `echo` (print/write file), `cat`, `ls`, `rm`, `clear`, `hex`, `ver`, `sleep`, `ata`, `crash`, `reboot`, `shutdown`. |

## Requirements

### Mandatory

| Tool | Purpose |
|------|---------|
| **NASM** | Assembles the bootloader to flat binary |
| **Clang** (with `-m32` support) | Compiles kernel C and assembly to 32-bit x86 code |
| **GNU ld.bfd** | Links ELF kernel image |
| **GNU Make** | Orchestrates the build |
| **QEMU** (system-x86_64) | Emulates the x86 machine |

### Installing dependencies

**Linux (Debian/Ubuntu):**

```sh
sudo apt install build-essential clang nasm qemu-system-x86 mtools
```

## Build

```sh
make
```

Produces `build/os-image.bin` — a 1.44 MB floppy image.

### Make targets

| Target | Action |
| --- | --- |
| `make` / `make all` | Build `os-image.bin` |
| `make clean` | Remove entire `build/` directory |
| `make run-qemu` | Build and launch QEMU with the image |

## Run

```sh
make run-qemu
```

Or directly:

```sh
qemu-system-x86_64 -boot order=a \
  -drive format=raw,file=build/os-image.bin,if=floppy \
  -drive file=disk.img,format=raw,if=none,id=ata0 \
  -device ide-hd,drive=ata0 -m 32 -serial stdio
```

- Add `-display gtk` for graphical window (omit for serial-only console output).
- `disk.img` is a 16 MB raw image formatted as FAT16 (superfloppy, no MBR partition table).
- All file operations persist to `disk.img` across reboots.

## Shell

```
Noveris OS v0.1
================
Type 'help' for commands.

Noveris$ help
Noveris OS Shell
----------------
clear    Clear screen
echo     Print text or write file (echo text > file)
cat      Display file contents
ls       List files
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
| `echo` (write) | `echo text > file` | Write text to a FAT16 file (creates or overwrites) |
| `cat` | `cat <file>` | Display file contents |
| `ls` | `ls` | List FAT16 root directory |
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
│   └── bootloader.asm        # MBR bootloader (real-mode → PMode trampoline)
├── kernel/
│   ├── entry.S               # Kernel entry (_start), BSS zeroing
│   ├── kernel.c              # Shell loop, command parsing, readline
│   ├── cpu/
│   │   ├── gdt.c/h           # GDT entries
│   │   ├── idt.c/h           # IDT entries, exception handler, register dump
│   │   ├── interrupt.S       # ISR/IRQ stubs (GAS macros)
│   │   ├── ports.h           # inb/outb/inw/outw inline asm
│   │   ├── timer.c/h         # PIT driver
│   ├── memory/
│   │   ├── pfa.c/h           # Page Frame Allocator (bitmap, 32MB)
│   │   ├── paging.c/h        # Paging (PD/PT, identity map, CR0.PG)
│   │   ├── heap.c/h          # Heap allocator (malloc/free, boundary tags)
│   └── drivers/
│       ├── ata.c/h           # ATA PIO driver (LBA28, IDENTIFY)
│       ├── fat16.c/h         # FAT16 filesystem driver
│       ├── keyboard.c/h      # PS/2 keyboard driver
│       ├── screen.c/h        # VGA text mode driver
│       ├── serial.c/h        # COM1 serial driver
├── build/                    # Build artifacts
├── disk.img                  # 16 MB FAT16 disk image
├── linker.ld                 # ELF linker script
├── Makefile                  # Build system
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
  ├── Real-mode setup, INT 0x13 loads kernel sectors to 0x0600
  ├── A20 gate enable (port 0x92)
  ├── GDT load, CR0 bit 0 set
  └── Far jump to PM trampoline at 0x0500

PM Trampoline (32-bit)
  ├── Reload segment registers with DATA_SEG (0x10)
  ├── Set ESP = 0x90000
  ├── Backwards rep movsd: copy kernel 0x0600 → 0x2000
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
  ├── fat_mount() — mount FAT16 volume
  └── Shell loop
```

## Key notes

- **No standard library** — kernel is `-ffreestanding -nostdlib`. Custom `strlen`/`strcpy`/`strcmp`.
- **BSS zeroing** in entry.S prevents crashes when static variables extend past loaded sectors.
- **Paging** uses identity mapping (virt = phys) for first 32MB. Page directory at dynamically allocated physical frame. `map_page()` supports custom non-identity mappings.
- **Heap** at 0x800000–0xA00000 (2MB, identity mapped). Boundary tag allocator: each block has a 4-byte header (size + LSB alloc flag) and 4-byte footer for O(1) backward merge. Minimum allocation 12 bytes.
- **ATA on secondary channel** — QEMU's `-device ide-hd` attaches to channel 1. The FAT driver probes all channels.
- **FAT16 superfloppy** — raw 16 MB image formatted with `mkfs.fat -F16`, no MBR partition table.
- **mtools access** — `mdir -i disk.img`, `mcopy -i disk.img file ::/`, etc.
