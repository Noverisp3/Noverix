# CODEBASE INDEX — Noverix OS

## 1. OVERALL ARCHITECTURE & DATA FLOW

**Model:** Monolithic Kernel — single binary loaded at 0x2000, no user/kernel space separation, bare-metal x86.

```
BIOS
 └─ boot/bootloader.asm (real-mode INT 0x13 load kernel → 0x9000)
    ├─ VBE init: INT 0x10 mode 0x118, info at 0x1000
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
             ├─ kernel/memory/heap.c   → malloc/free allocator
             ├─ kernel/drivers/ata.c   → ATA probe
             ├─ kernel/drivers/nvfs.c  → mount NVFS from ATA LBA
             └─ while(1): readline → handle_cmd → dispatch
```

**Disk data flow:** ATA PIO LBA28 → raw sector buffer → NVFS superblock parse → bitmap/inode management → extent-based data read/write. Write (shadow paging): save old extents → alloc new blocks → write data → inode_write (persist) → free old blocks → update bitmap.

**Shell input flow:** PS/2 keyboard IRQ1 (ring buffer) + COM1 serial poll (non-blocking) → `read_char_any()` → `readline()` → line buffer → `handle_cmd()` → dispatch.

## 2. DIRECTORY STRUCTURE

```
Project_002_OS/
├── boot/
│   └── bootloader.asm        # MBR: real-mode → A20 → GDT → PM trampoline
├── kernel/
│   ├── entry.S               # Entry: BSS zero → jmp kernel_main
│   ├── kernel.c              # Shell: readline, history, cmd dispatch, serial input
│   ├── cpu/
│   │   ├── gdt.c / gdt.h     # GDT (null,code,data,user_code,user_data)
│   │   ├── idt.c / idt.h     # IDT (32 ISR + 16 IRQ), register dump
│   │   ├── interrupt.S       # ISR/IRQ stubs, common handler asm
│   │   ├── ports.h           # inb/outb/inw/outw inline asm
│   │   ├── timer.c / timer.h # PIT channel 0, atomic tick, sleep_ms
│   ├── memory/
│   │   ├── pfa.c / pfa.h     # Page Frame Allocator (bitmap, 32MB)
│   │   ├── paging.c / paging.h # Paging (PD/PT, identity map, CR0.PG)
│   │   ├── heap.c / heap.h   # Heap allocator (malloc/free, boundary tags)
│   └── drivers/
│       ├── ata.c / ata.h     # ATA PIO: probe, LBA28 read/write
│       ├── nvfs.c / nvfs.h   # NVFS: extent-based filesystem driver
│       ├── keyboard.c / .h     # PS/2 IRQ1: scancode→ASCII, ring buf
│       ├── screen.c / .h     # VGA text + VBE graphics dispatch
│       ├── graphics.c / .h   # VBE framebuffer: pixel, rect, char, scroll
│       ├── font.h            # Generated 8×16 bitmap font (95 chars)
│       └── serial.c / .h    # COM1: init, putchar, puts, puthex, data_available, read_char
├── tools/
│   ├── mknvfs.py             # NVFS disk formatter (16MB, 32768 sectors)
│   └── genfont.py            # VGA 8×16 bitmap font → font.h generator
├── linker.ld                 # ELF linker: 0x2000, PHDRS RX/RW
├── Makefile                  # clang + nasm + ld.bfd + objcopy + python
├── noverix.img               # Combined disk (boot + kernel + NVFS)
├── nvfs_disk.img             # 16MB NVFS raw image
├── README.md
├── CODEBASE_INDEX.md
└── currentfeatures.md
```

## 3. DETAILED MODULE INDEX

---

### `boot/bootloader.asm`

- **Role:** MBR bootloader (512 bytes, loaded at 0x7C00). Real-mode → protected mode transition.
- **Functions/Macros:**
  - `print_string`: INT 0x10 teletype | [BIOS]
  - `disk_load`: INT 0x13 loads kernel from floppy/HDD to 0x9000 | [BIOS]
  - `enable_a20`: port 0x92 + keyboard controller 0x64/0x60 + INT 0x15 | [I/O ports]
  - `switch_to_pm`: GDT load → A20 → CR0 bit 0 → far jump 0x0500 | [gdt descriptor inline]
  - `pm_trampoline` (32-bit): reload segments, ESP=0x90000, forward rep movsd 0x9000→0x2000, call 0x2000 | []
- **Import:** Constants `KERNEL_OFFSET=0x2000`, `KERNEL_LOAD_ADDR=0x9000`, `PM_TRAMPOLINE_ADDR=0x0500`
- **VBE init:** After kernel load, calls INT 0x10 AX=0x4F01/CX=0x0118 (get mode info) then AX=0x4F02/BX=0x4118 (set mode 0x118 with LFB). Mode info buffer at `0x0000:0x0600` to avoid overwriting kernel load area. LFB/width/height/pitch/bpp stored at `0x1000` for kernel consumption.
- **Notes:** Forward copy (`cld` `rep movsd`) is safe because dest (0x2000) < src (0x9000) — source is always read before dest overwrites, even when overlapping (kernel >56 sectors). Replaces the old backward copy (`std`) which corrupted the source when dest overlapped source.

---

### `kernel/entry.S`

- **Role:** Kernel entry point (0x2000). Zeroes BSS before calling C code.
- **Functions:**
  - `_start`: Zero BSS (bss_start→bss_end via `rep stosl`) → `jmp kernel_main`
- **Import:** `kernel_main` (C), `bss_start`, `bss_end` (linker.ld)

---

### `kernel/kernel.c`

- **Role:** Main shell. Initializes all subsystems, command loop.
- **Functions:**
  - `strlen(s)`: Length | []
  - `strcpy(dst, src)`: Copy | []
  - `strcmp(a, b)`: Compare | []
  - `reboot()`: Send 0xFE to port 0x64 | [inb, outb]
  - `shutdown()`: Write 0x2000 to port 0xB004 + 0x604 | [outw]
   - `history_add(buf)`: Add command to history ring (16 entries), with overflow protection (shifts oldest when full) | [strcpy, strcmp]
   - `readline(buf, max)`: Read input from keyboard + serial (via `read_char_any`), inline editing (LEFT/RIGHT move, mid-line insert/delete), UP/DOWN history, Ctrl+C (char 0x03) cancels line and prints ^C | [read_char_any, print_string, history_add]
   - `read_char_any(void)`: Read char from keyboard (get_char) or serial (serial_read_char) — non-blocking | [get_char, serial_data_available, serial_read_char]
   - `execute_cmd(cmd, arg)`: Dispatch parsed command (extracted from handle_cmd for pipe reuse). Echo redirection has bounds checking on `>` operator. Hex/sleep commands have integer overflow guards | [strcmp, print_string, clear_screen, print_hex, sleep_ms, nvfs_list, nvfs_chdir, nvfs_read, nvfs_write, nvfs_delete, nvfs_mkdir, nvfs_rmdir, reboot, shutdown]
   - `handle_cmd(buf)`: Parse `|` pipe → split left/right → `set_capture(1)` → `execute_cmd(cmd1, arg1)` → `set_capture(0)` → copy captured output to `pipe_data` → `execute_cmd(cmd2, arg2)` | [execute_cmd, set_capture, get_capture]
   - `kernel_main(void)`: Init sequence → shell loop | [init_serial, init_gdt, init_idt, init_screen, init_keyboard, init_timer, pfa_init, init_paging, heap_init, ata_init, nvfs_mount]
- **Static data:** `history[HISTORY_SIZE][LINE_BUF]`, `pipe_data[4096]`, `has_pipe_data`
- **VBE init:** After paging, reads VBE info at `0x1000` (LFB, width, height, pitch, bpp). If LFB is non-zero, maps the framebuffer into page tables via `map_page()` (576 pages for 1024×768×24bpp), then calls `init_graphics()` to activate graphics mode. Falls back to text mode if LFB is zero (VBE unavailable).
- **Import:** `screen.h`, `keyboard.h`, `serial.h`, `ata.h`, `nvfs.h`, `gdt.h`, `idt.h`, `timer.h`, `ports.h`, `graphics.h`
- **New shell features over FAT16 version:** `mkdir`, `rmdir`, `cd` (with path, `..`, `./..`, `/`). Dynamic prompt showing current path (e.g. `/MYDIR$`). `>>` append operator. `|` pipe operator. Specific error messages via `nvfs_strerror(nvfs_errno)`.
- **Pipe flow:** `set_capture(1)` → print_*/print_string redirect to 4KB capture buffer → `set_capture(0)` → `get_capture()` → copy to `pipe_data` → set `has_pipe_data=1` → execute cmd2 (cat/echo read pipe_data when arg is empty).
- **Ctrl+C:** When `readline` receives char 0x03, it prints `^C\n` and returns an empty buffer.
- **Serial input:** `readline` polls both keyboard (IRQ1 ring buffer) and COM1 serial (non-blocking poll). Allows command piping via `-serial stdio`.

---

### `kernel/cpu/gdt.c` + `gdt.h`

- **Role:** Initialize GDT with 5 entries (null, code, data, user_code, user_data). Reload segments.
- **Struct:** `gdt_entry_t`, `gdt_ptr_t` (packed)
- **Functions:**
  - `gdt_set_entry(num, base, limit, access, gran)`: Write descriptor | []
  - `init_gdt(void)`: Set 5 entries → `lgdt` → inline asm reload segment regs + ljmp flush | [ports.h]
- **Import:** `ports.h`

---

### `kernel/cpu/idt.c` + `idt.h`

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
  - `irq_handler(regs)`: Send EOI → handler | [outb]
  - `irq_remap(void)`: PIC master+slave remap (ICW1-ICW4) | [outb]
   - `register_interrupt_handler(irq, handler)`: Atomic pushf/cli/popf → set handler | []
  - `init_idt(void)`: Set 256 entries → LIDT → irq_remap → STI | [idt_set_entry, irq_remap]
- **Import:** `ports.h`, `screen.h`, `serial.h`, 32 extern ISR labels + 16 extern IRQ labels (interrupt.S)

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

### `kernel/cpu/timer.c` + `timer.h`

- **Role:** PIT channel 0 tick counter. `sleep_ms` function. Provides time source for file timestamps.
- **Functions:**
  - `timer_handler(regs)`: Increment `tick_count` by 1 | []
  - `init_timer(freq)`: Set divisor, register IRQ0 handler | [outb, register_interrupt_handler]
  - `get_ticks(void)`: Return tick_count | []
  - `sleep_ms(ms)`: Busy-wait based on tick difference | []
- **Import:** `idt.h`, `ports.h`

---

### `kernel/memory/pfa.c` + `pfa.h`

- **Role:** Page Frame Allocator — bitmap-based physical memory manager.
- **Static data:** `bitmap[1024]` (8192 bits for 32MB, 1 bit per 4KB frame)
- **Functions:**
  - `pfa_init(void)`: Mark reserved frames (null page, kernel, stack, legacy 0xA0000-0xFFFFF) via loop starting at 0x00000000 | [set_bit, serial_write_string]
  - `alloc_frame(void)`: Scan bitmap → first 0 bit → set to 1 → return physical address | [test_bit, set_bit]
  - `free_frame(addr)`: Clear corresponding bit | [clear_bit]
  - `set_bit(frame)`: Internal — set bit in bitmap | []
  - `clear_bit(frame)`: Internal — clear bit | []
  - `test_bit(frame)`: Internal — test bit | []
  - `mark_frame(addr)`: Internal — set bit for frame containing addr | [set_bit]
- **Import:** `serial.h`, `bss_end` (linker symbol)

---

### `kernel/memory/paging.c` + `paging.h`

- **Role:** 32-bit x86 two-level paging. Identity map 0-32MB, enable CR0.PG.
- **Static data:** `page_dir` (1024 PDEs, allocated from PFA)
- **Functions:**
  - `init_paging(void)`: Alloc PD + first PT + PTs 4-32MB → load CR3 → set CR0.PG | [alloc_frame, create_table]
  - `create_table(virt, flags)`: Internal — alloc page table, set PDE | [alloc_frame]
  - `map_page(virt, phys, flags)`: Map virtual → physical, create PT if needed, invlpg | [create_table]
  - `read_cr0(void)`: Return CR0 | []
  - `read_cr3(void)`: Return CR3 | []
- **Import:** `pfa.h`, `serial.h`

---

### `kernel/memory/heap.c` + `heap.h`

- **Role:** Kernel heap allocator — boundary-tag first-fit malloc/free.
- **Constants:** `HEAP_START=0x800000`, `HEAP_SIZE=0x200000` (2MB region, identity mapped)
- **Functions:**
  - `heap_init(void)`: Create 1 free block covering entire HEAP_SIZE | [set_footer, serial_write_string]
  - `malloc(size)`: Walk blocks → first-fit → split if remainder >= MIN_BLOCK (12) → return ptr after header | [set_footer]
  - `free(ptr)`: Mark free → merge with next block (if free) → merge with prev block via boundary tag footer (with bounds guard) | [set_footer]
  - `set_footer(addr, size)`: Write size at block end (for boundary tag) | []
- **Import:** `serial.h`
- **Bug fix:** Removed `prev_addr = (unsigned int)prev_hdr;` in backward merge (`free()`). This line assigned `prev_addr` the address of a local stack pointer instead of the heap block address, causing wrong footer and heap corruption.

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
  - `find_drive(void)`: Find first ATA device | [ata_drive_exists]
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
  - `nvfs_mount(void)`: Read superblock → set sb_* fields → set nvfs_cwd = root | [find_drive, read_sector]
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
- **Static data:** `key_buffer[256]` (ring), `shift_pressed`, `caps_on`, `extended` flag
- **Tables:** `scancode_ascii[58]`, `scancode_ascii_shift[58]`
- **Functions:**
  - `keyboard_handler(regs)`: Read scancode → handle extended (0xE0), shift, caps → push ASCII/keycode to ring buffer | [inb, outb]
  - `get_char(void)`: Get 1 char from ring buffer (non-blocking) | []
  - `read_char(void)`: Blocking get_char | [get_char]
  - `init_keyboard(void)`: Reset state, register IRQ1 handler | [register_interrupt_handler]
- **Import:** `ports.h`, `idt.h`

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
  - `fb_cols(void)`: Return `fb_width / FONT_WIDTH` | []
  - `fb_rows(void)`: Return `fb_height / FONT_HEIGHT` | []
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
