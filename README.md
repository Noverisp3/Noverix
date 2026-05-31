# Noveris OS

A minimal x86 hobby operating system built from scratch. Boots from real mode into 32-bit protected mode and runs a command-line shell. Designed for educational purposes and emulated with Bochs.

## Features

| Area | Details |
|------|---------|
| **Boot** | Real-mode → protected-mode transition, A20 gate enable, GDT loading |
| **Disk I/O** | INT 0x13 with CHS addressing, multi-track reads |
| **Kernel loading** | Safe overlapping copy via backwards `rep movsd` (source `0x0600` → dest `0x2000`) |
| **VGA text mode** | 80×25, hardware cursor, scrolling, hex output, `\t` tab stops |
| **PS/2 keyboard** | Ring buffer, shift/caps lock, arrow key support, extended scancode handling |
| **Interrupts** | IDT with 32 ISRs (exceptions) and 16 IRQs (PIC remapped to INT 32–47), handler registration |
| **Shell** | Command history (up/down arrows), `help`, `echo`, `clear`, `hex`, `ver`, `sleep`, `reboot`, `shutdown`/`poweroff` |

## Requirements

### Mandatory

| Tool | Purpose |
|------|---------|
| **NASM** | Assembles the bootloader (`boot/bootloader.asm`) to flat binary |
| **GCC** (i686, `-m32`) | Compiles kernel C and assembly to 32-bit code |
| **GNU Make** | Orchestrates the build |
| **Bochs** 2.x–3.x | Emulates the x86 machine; runs `build/os-image.bin` as a floppy |
| **PowerShell 5+** | Used by `combine.ps1` (image padding) and `Makefile` (kernel sector count detection) |

### Installing a toolchain

**Linux (Debian/Ubuntu):**

```sh
sudo apt install build-essential nasm bochs bochs-x
```

**Windows (MSYS2):**

Install [MSYS2](https://www.msys2.org/), open the *UCRT64* or *MINGW64* terminal, and run:

```sh
pacman -S mingw-w64-x86_64-nasm mingw-w64-x86_64-gcc make
```

Install Bochs from [bochs.sourceforge.net](https://bochs.sourceforge.net/) (default path: `C:\Program Files\Bochs-3.0\`). The bundled `run.bat` launches it automatically.

**Cross-compiler (recommended for serious development):**

Building an `i686-elf` cross-compiler avoids dependency on the host GCC's libc and startup files. Follow the [OSDev GCC Cross-Compiler](https://wiki.osdev.org/GCC_Cross-Compiler) tutorial. After building, either:

- Edit `CC` and `LD` in the Makefile to point at `i686-elf-gcc`, or
- Add the cross-compiler `bin/` directory to your `PATH`.

The current Makefile uses plain `gcc` with `-ffreestanding -nostdlib` flags, which works with a cross-compiler or a MinGW/GCC that supports `-m32`.

## Build

```sh
make
```

Produces `build/os-image.bin` — a 1.44 MB floppy image.

### Make targets

| Target | Action |
|--------|--------|
| `make` / `make all` | Build `os-image.bin` |
| `make clean` | Remove entire `build/` directory |
| `make run` | Build and launch Bochs with the image |

### What the build does (step by step)

1. **Assemble bootloader** — `nasm -f bin boot/bootloader.asm` → `build/bootloader.bin` (exactly 512 bytes, MBR with `0xAA55` signature). The `-dKERNEL_SECTORS=N` flag tells the bootloader how many disk sectors to load.
2. **Compile kernel objects** — `gcc -ffreestanding -m32 -c` each `.c` file → `.o` files. Assembly stubs (`entry.S`, `interrupt.S`) are assembled with `gcc -m32 -c`.
3. **Link kernel ELF** — `gcc` with linker flags `-nostdlib -e _start -Wl,--image-base,0x1000 -Wl,--file-alignment,0x200` links all kernel objects into `build/kernel.elf`.
4. **Strip to flat binary** — `objcopy -O binary kernel.elf` → `kernel.bin`. This removes ELF headers, leaving raw 32-bit code that will be loaded at `0x2000`.
5. **Combine into floppy image** — `combine.ps1` concatenates the bootloader and kernel binary, then zero-pads the result to exactly 1,474,560 bytes (a 1.44 MB floppy).

### Kernel sector count

The bootloader must know how many 512-byte sectors the kernel occupies. The Makefile computes this dynamically with:

```makefile
KERNEL_SECTORS = $(shell powershell ... -Command "[math]::Ceiling((Get-Item kernel.bin).Length/512)")
```

This value is passed to NASM as `-dKERNEL_SECTORS=N` and defaults to 64 if `kernel.bin` is not yet built.

## Run

```sh
make run
```

Or directly:

```sh
bochs -q -f bochsrc.bxrc
```

On Windows: `run.bat` (runs `make`, then launches Bochs).

### Bochs configuration (`bochsrc.bxrc`)

- **CPU**: 32 MB RAM (`megs: 32`)
- **Boot**: floppy drive, `build/os-image.bin`
- **Display**: Bochs internal VGABIOS (no mouse)
- **Log**: written to `bochsout.txt`

Press **Ctrl-C** in the Bochs console or close the window to stop the emulator.

## Boot process

```
┌─────────────────────────────────────────────────────────┐
│  BIOS → 0x7C00 (bootloader MBR)                         │
│    • Sets up segments and stack at 0x9000               │
│    • Loads kernel (KERNEL_SECTORS sectors) from disk    │
│      to staging buffer at 0x0600 via INT 0x13 (CHS)     │
│    • Copies PM trampoline to 0x0500 (safe zone)         │
│    • Enables A20 gate via port 0x92                     │
│    • Loads GDT (code 0x08, data 0x10)                   │
│    • Sets CR0 bit 0 (protected mode)                    │
│    • Far-jumps to PM trampoline at 0x0500               │
├─────────────────────────────────────────────────────────┤
│  PM Trampoline (32-bit, executes at 0x0500)             │
│    • Reloads segment registers with DATA_SEG (0x10)     │
│    • Sets up stack pointer to 0x9000                    │
│    • Backwards rep movsd: copies kernel from 0x0600     │
│      to 0x2000 (count = KERNEL_SECTORS * 128 dwords)    │
│    • Calls KERNEL_OFFSET (0x2000) → kernel_main()       │
├─────────────────────────────────────────────────────────┤
│  kernel_main() (C code)                                 │
│    • init_gdt()   — reloads GDT with user-mode entries  │
│    • init_idt()   — sets up IDT, remaps PIC, enables    │
│                     interrupts (STI)                    │
│    • init_screen() — clears screen, resets cursor       │
│    • init_keyboard() — registers IRQ1 handler           │
│    • Prints banner, enters shell read-eval loop         │
└─────────────────────────────────────────────────────────┘
```

## Memory map

| Address | Size | Contents |
|---------|------|----------|
| `0x0500` | ~256 B | PM trampoline (32-bit code, copied there by bootloader) |
| `0x0600` | variable | Kernel staging buffer (loaded from disk) |
| `0x2000` | variable | Kernel destination (copy target, executed from here) |
| `0x7C00` | 512 B | Bootloader MBR |
| `0x7E00`–`0x8FFF` | ~4.5 KB | Bootloader variables/data |
| `0x9000` | 28 KB | Stack (grows downward) |
| `0xB8000` | 4 KB | VGA text mode framebuffer (80×25 × 2 bytes per char) |

### VGA framebuffer layout

Each character cell at `0xB8000` is 2 bytes:

```
Bit  15–8   |   7–0
Attribute    |   ASCII
```

Attribute byte: `0x0F` = white on black (foreground = bright white, background = black).

## Shell

```
Noveris OS v0.1
================
Type 'help' for commands.

Noveris$ help
Noveris OS Shell
----------------
help     Show this help
echo     Print text
clear    Clear screen
hex      Print a number in hex
ver      Show version
reboot   Reboot system
shutdown Power off
```

- **Up/Down arrows** cycle through the last 16 commands
- **Backspace** deletes the character before the cursor
- `shutdown` and `poweroff` are aliases

### Commands reference

| Command | Syntax | Description |
|---------|--------|-------------|
| `help`, `?` | `help` | Print available commands |
| `echo` | `echo <text>` | Print `<text>` to screen |
| `clear` | `clear` | Clear the screen and reset cursor |
| `hex` | `hex <num>` | Parse a decimal number and print it in hex |
| `ver` | `ver` | Print OS version string |
| `reboot` | `reboot` | Send reset signal via the keyboard controller (port 0x64, command 0xFE) |
| `shutdown`, `poweroff` | `shutdown` | Power off via Bochs/QEMU-specific ACPI ports (0xB004, 0x604) |

## Project layout

```
.
├── boot/
│   └── bootloader.asm        # 512-byte MBR bootloader (real-mode, PM trampoline)
├── kernel/
│   ├── entry.S               # Kernel entry point (_start → kernel_main)
│   ├── kernel.c              # Shell loop, command parsing, readline with history
│   ├── cpu/
│   │   ├── gdt.c             # GDT entries (null, code, data, user code, user data)
│   │   ├── gdt.h
│   │   ├── idt.c             # IDT entries, ISR/IRQ setup, PIC remap, handler registration
│   │   ├── idt.h             # registers_t struct, interrupt_handler_t typedef
│   │   ├── interrupt.S       # ISR and IRQ stubs (GAS macros), common handler
│   │   └── ports.h           # inb/outb/inw/outw inline asm helpers
│   │   ├── timer.c           # Programmable Interval Timer (PIT)
│   │   └── timer.h
│   └── drivers/
│       ├── keyboard.c        # PS/2 driver: scancode→ASCII, shift/caps, ring buffer
│       ├── keyboard.h        # KEY_UP/DOWN/LEFT/RIGHT constants
│       ├── screen.c          # VGA text mode: cursor, scroll, print_char/string/hex
│       └── screen.h
│       ├── serial.c          # COM1 serial port driver for debugging/logging
│       └── serial.h
├── build/                    # Build artifacts (.o, .bin, .elf, os-image.bin)
├── bochsrc.bxrc              # Bochs emulator configuration
├── combine.ps1               # PowerShell script: concatenate + pad to 1.44 MB
├── currentfeatures.md        # Quick summary of implemented features
├── Makefile                  # Build system
└── run.bat                   # Windows shortcut: make + bochs
```

## Key design notes

- **No standard library** — the kernel is `-ffreestanding -nostdlib`. Custom `strlen`/`strcpy`/`strcmp` are in `kernel.c`.
- **Backwards copy** — The kernel is loaded to `0x0600` but needs to run at `0x2000`. Since `0x0600` < `0x2000` and the regions overlap, a forward copy would corrupt the source. The trampoline uses `std` + `rep movsd` (decrementing addresses) to copy from high to low.
- **PIC remap** — The Programmable Interrupt Controller (8259A) is remapped so IRQs 0–15 appear at INT 32–47 (instead of 8–15 which conflict with CPU exceptions).
- **Multi-track disk reads** — The bootloader's CHS loop handles the floppy geometry: 18 sectors/track, 2 heads, 80 cylinders. Sectors count from 1 on each track, and sector 1 (boot sector) is skipped.
- **A20 gate** — Enabled via the keyboard controller (port 0x92, bit 1). This grants access to the second 1 MB of memory (needed for the VGA framebuffer at `0xB8000` and beyond).

## Port I/O cheat sheet

| Port | Direction | Purpose |
|------|-----------|---------|
| `0x60` | Read | Keyboard data port (scancode) |
| `0x64` | Read | Keyboard status register (bit 0 = output buffer full) |
| `0x64` | Write | Keyboard command (e.g. `0xFE` = system reset) |
| `0x3D4` | Write | VGA CRTC address register |
| `0x3D5` | Write | VGA CRTC data register |
| `0x20` | Write | Master PIC command (EOI = `0x20`) |
| `0xA0` | Write | Slave PIC command (EOI = `0x20`) |
| `0x21` | Write | Master PIC data (ICW2 = `0x20` → IRQs at INT 32+) |
| `0xA1` | Write | Slave PIC data (ICW2 = `0x28` → IRQs at INT 40+) |
| `0xB004` | Write | Bochs ACPI shutdown (`0x2000`) |
| `0x604` | Write | QEMU ACPI shutdown (`0x2000`) |
| `0x92` | Write | System control port (bit 1 = A20 gate) |
