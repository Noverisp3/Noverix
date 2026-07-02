## Current Features

### Boot
- Real mode → protected mode (A20 gate, GDT)
- Disk load via INT 0x13 (CHS, multi-track)
- Backwards `rep movsd` kernel copy (safe overlapping source/dest)
- PM trampoline at `0x0500` (below staging buffer)

### Shell Commands
| Command | Description |
|---------|-------------|
| `help` / `?` | Show this help |
| `echo <text>` | Print text |
| `echo text > file` | Write text to FAT16 file (creates or overwrites) |
| `cat <file>` | Display file contents |
| `ls` | List files |
| `rm <file>` | Delete file |
| `clear` | Clear screen |
| `hex <num>` | Print number in hexadecimal |
| `ver` | Show OS version |
| `sleep <ms>` | Sleep for N milliseconds |
| `ata` | List ATA drives |
| `crash` | Trigger exception with register dump |
| `reboot` | Reboot system |
| `shutdown` / `poweroff` | Power off |

### Drivers
- **PS/2 keyboard** — ring buffer, shifted/caps scan codes, arrow keys
- **VGA text mode** — 80×25, cursor, scrolling, hex/dec output
- **Serial (COM1)** — early boot logging, hex output
- **ATA PIO (LBA28)** — dual-channel primary/secondary, IDENTIFY probe, read/write
- **FAT16** — BPB parsing, root directory, cluster chain, file CRUD with FAT1/FAT2 mirror

### Memory Management
- **Page Frame Allocator** — bitmap-based, 1 bit per 4KB frame, 8192 frames (32MB)
- **Paging** — 32-bit x86 two-level (PD+PT), identity map 0–32MB, `map_page()` for custom mappings
- **Heap** — boundary-tag first-fit `malloc`/`free` at 0x800000 (2MB), split/coalesce, OOM logging

### Interrupts
- IDT with 32 exception ISRs + 16 IRQs, PIC remap
- Exception handler with full register dump (screen + serial)
- PIT timer with atomic tick counter (`LOCK XADD`), `sleep_ms()`

### Build
- All artifacts to `build/`, source tree clean
- Floppy image (1.44MB, bootloader + kernel)
- QEMU and Bochs emulation targets
