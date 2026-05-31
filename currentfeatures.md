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
| `clear` | Clear screen |
| `hex <num>` | Print number in hexadecimal |
| `ver` | Show OS version |
| `reboot` | Reboot system |
| `shutdown` / `poweroff` | Power off |

### Drivers
- **PS/2 keyboard** — ring buffer, shifted/caps scan codes, arrow keys
- **VGA text mode** — 80×25, cursor, scrolling, hex output

### Build
- All artifacts to `build/`, source tree clean
- Floppy image (1.44MB, bootloader + kernel)
- Bochs emulation (32MB RAM)
