## Current Features

### Boot
- Real mode → protected mode (A20 gate, GDT)
- Disk load via INT 0x13 (CHS, multi-track)
- Forward `rep movsd` kernel copy (safe overlapping source/dest)
- PM trampoline at `0x0500` (below staging buffer)

### Shell Commands
| Command | Description |
|---------|-------------|
| `help` / `?` | Show this help |
| `echo <text>` | Print text |
| `echo text > file` | Write text to file (creates or overwrites) |
| `cat <file>` | Display file contents |
| `ls [path]` | List files/directories (`[DIR]` tag + `<DIR>` label) |
| `cd [path]` | Change directory (supports `/`, `..`, `./..`, `.`) |
| `mkdir <path>` | Create directory |
| `rmdir <path>` | Remove empty directory |
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
- **Serial (COM1)** — boot logging, hex output, shell input (poll via `serial_data_available`)
- **ATA PIO (LBA28)** — dual-channel primary/secondary, IDENTIFY probe, read/write
- **NVFS** — extent-based filesystem: superblock, block bitmap, 128 inodes, 32726 data blocks. Inode stores up to 14 extents (start + count). No cluster chain walking.

### Memory Management
- **Page Frame Allocator** — bitmap-based, 1 bit per 4KB frame, 8192 frames (32MB)
- **Paging** — 32-bit x86 two-level (PD+PT), identity map 0–32MB, `map_page()` for custom mappings
- **Heap** — boundary-tag first-fit `malloc`/`free` at 0x800000 (2MB), split/coalesce, OOM logging

### Interrupts
- IDT with 32 exception ISRs + 16 IRQs, PIC remap
- Exception handler with full register dump (screen + serial)
- PIT timer with atomic tick counter (`LOCK XADD`), `sleep_ms()`

### Shell UX
- Command history (UP/DOWN)
- Inline editing (LEFT/RIGHT, Backspace, insert/delete)
- Dynamic prompt showing current directory path (`/$`, `/MYDIR$`)
- Dual input: PS/2 keyboard + COM1 serial (via pipe/script)

### Build
- All artifacts to `build/`, source tree clean
- Floppy image (1.44MB, bootloader + kernel)
- NVFS disk image via `tools/mknvfs.py` (16MB, 32768 sectors)
- QEMU and Bochs emulation targets
