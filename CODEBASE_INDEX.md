# CODEBASE INDEX — Noveris OS

## 1. KIẾN TRÚC TỔNG QUAN & LUỒNG DỮ LIỆU

**Mô hình:** Monolithic Kernel — single binary loaded at 0x2000, no user/kernel space separation, bare-metal x86.

```
BIOS
 └─ boot/bootloader.asm (real-mode INT 0x13 load kernel → 0x9000)
    └─ PM trampoline (forward copy 0x9000 → 0x2000)
       └─ kernel/entry.S (BSS zeroing)
          └─ kernel/kernel.c::kernel_main (C entry)
             ├─ kernel/cpu/gdt.c       → GDT reload
             ├─ kernel/cpu/idt.c       → IDT + PIC remap + exceptions
             ├─ kernel/drivers/screen.c → VGA text mode
             ├─ kernel/drivers/keyboard.c → PS/2 IRQ1
             ├─ kernel/cpu/timer.c     → PIT IRQ0
             ├─ kernel/memory/pfa.c    → page frame allocator
             ├─ kernel/memory/paging.c → identity map 32MB, enable CR0.PG
             ├─ kernel/memory/heap.c   → malloc/free allocator
             ├─ kernel/drivers/ata.c   → ATA probe
             ├─ kernel/drivers/nvfs.c  → mount NVFS from ATA LBA
             └─ while(1): readline → handle_cmd → dispatch
```

**Luồng dữ liệu disk:** ATA PIO LBA28 → raw sector buffer → NVFS superblock parse → bitmap/inode management → extent-based data read/write. Ghi: memory buffer → extent alloc → inode update → data write → bitmap update.

**Luồng shell input:** PS/2 keyboard IRQ1 (ring buffer) + COM1 serial poll (non-blocking) → `read_char_any()` → `readline()` → line buffer → `handle_cmd()` → dispatch.

## 2. SƠ ĐỒ CẤU TRÚC THƯ MỤC

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
│       ├── keyboard.c / .h   # PS/2 IRQ1: scancode→ASCII, ring buf
│       ├── screen.c / .h     # VGA 80×25: print, scroll, cursor
│       └── serial.c / .h    # COM1: init, putchar, puts, puthex, data_available, read_char
├── tools/
│   └── mknvfs.py             # NVFS disk formatter (16MB, 32768 sectors)
├── linker.ld                 # ELF linker: 0x2000, PHDRS RX/RW
├── Makefile                  # clang + nasm + ld.bfd + objcopy + python
├── noverix.img               # Combined disk (boot + kernel + NVFS)
├── nvfs_disk.img             # 16MB NVFS raw image
├── README.md
└── currentfeatures.md
```

## 3. CHỈ MỤC CHI TIẾT (MODULES, CLASSES & FUNCTIONS INDEX)

---

### `boot/bootloader.asm`

- **Vai trò:** MBR bootloader (512 bytes, loaded at 0x7C00). Chuyển real-mode → protected mode.
- **Hàm/Macro:**
  - `print_string`: INT 0x10 teletype | [BIOS]
  - `disk_load`: INT 0x13 CHS đọc kernel từ floppy vào 0x9000 | [BIOS]
  - `enable_a20`: port 0x92 + keyboard controller 0x64/0x60 + INT 0x15 | [I/O ports]
  - `switch_to_pm`: GDT load → A20 → CR0 bit 0 → far jump 0x0500 | [gdt descriptor inline]
  - `pm_trampoline` (32-bit): reload segments, ESP=0x90000, forward rep movsd 0x9000→0x2000, call 0x2000 | []
- **Import:** Constants `KERNEL_OFFSET=0x2000`, `KERNEL_LOAD_ADDR=0x9000`, `PM_TRAMPOLINE_ADDR=0x0500`
- **Ghi chú:** Forward copy (`cld` `rep movsd`) an toàn vì dest (0x2000) < src (0x9000) — source luôn được đọc trước khi dest ghi đè, kể cả khi có overlap (kernel >56 sectors). Backward copy (`std`) cũ được thay thế vì corrupt source khi dest overlap source.

---

### `kernel/entry.S`

- **Vai trò:** Điểm vào kernel (0x2000). Zero BSS trước khi gọi C.
- **Hàm:**
  - `_start`: Zero BSS (bss_start→bss_end bằng `rep stosl`) → `jmp kernel_main`
- **Import:** `kernel_main` (C), `bss_start`, `bss_end` (linker.ld)

---

### `kernel/kernel.c`

- **Vai trò:** Shell chính. Khởi tạo toàn bộ subsystems, vòng lặp đọc lệnh.
- **Hàm:**
  - `strlen(s)`: Length | []
  - `strcpy(dst, src)`: Copy | []
  - `strcmp(a, b)`: Compare | []
  - `reboot()`: Gửi 0xFE đến port 0x64 | [inb, outb]
  - `shutdown()`: Ghi 0x2000 vào port 0xB004 + 0x604 | [outw]
  - `history_add(buf)`: Thêm lệnh vào lịch sử (mảng 16 phần tử) | [strcpy, strcmp]
  - `readline(buf, max)`: Đọc input từ keyboard + serial (qua `read_char_any`), inline editing (LEFT/RIGHT di chuyển, insert/delete giữa dòng), UP/DOWN history | [read_char_any, print_string, history_add]
  - `read_char_any(void)`: Đọc ký tự từ keyboard (get_char) hoặc serial (serial_read_char) — non-blocking | [get_char, serial_data_available, serial_read_char]
  - `handle_cmd(buf)`: Parse cmd/arg, dispatch | [strcmp, print_string, clear_screen, print_hex, sleep_ms, nvfs_list, nvfs_chdir, nvfs_read, nvfs_write, nvfs_delete, nvfs_mkdir, nvfs_rmdir, reboot, shutdown]
  - `kernel_main(void)`: Init sequence → shell loop | [init_serial, init_gdt, init_idt, init_screen, init_keyboard, init_timer, pfa_init, init_paging, heap_init, ata_init, nvfs_mount]
- **Import:** `screen.h`, `keyboard.h`, `serial.h`, `ata.h`, `nvfs.h`, `gdt.h`, `idt.h`, `timer.h`, `ports.h`
- **Lệnh shell mới so với phiên bản FAT16:** `mkdir`, `rmdir`, `cd` (với path, `..`, `./..`, `/`). Prompt động hiển thị path hiện tại (vd `/MYDIR$`).
- **Serial input:** `readline` poll cả keyboard (IRQ1 ring buffer) và COM1 serial (poll non-blocking). Cho phép pipe lệnh qua `-serial stdio`.

---

### `kernel/cpu/gdt.c` + `gdt.h`

- **Vai trò:** Khởi tạo GDT 5 entries (null, code, data, user_code, user_data). Reload segments.
- **Struct:** `gdt_entry_t`, `gdt_ptr_t` (packed)
- **Hàm:**
  - `gdt_set_entry(num, base, limit, access, gran)`: Ghi descriptor | []
  - `init_gdt(void)`: Set 5 entries → `lgdt` → inline asm reload segment regs + ljmp flush | [ports.h]
- **Import:** `ports.h`

---

### `kernel/cpu/idt.c` + `idt.h`

- **Vai trò:** IDT 256 entries (32 exception + 16 IRQ). PIC remap. Exception handler + register dump.
- **Struct/Type:**
  - `idt_entry_t`, `idt_ptr_t` (packed)
  - `registers_t`: {gs, fs, es, ds, edi, esi, ebp, esp, ebx, edx, ecx, eax, int_no, err_code, eip, cs, eflags, useresp, ss}
  - `interrupt_handler_t`: function pointer `void (*)(registers_t*)`
- **Hàm:**
  - `idt_set_entry(num, base, sel, flags)`: Ghi IDT entry | []
  - `dump_registers(regs)`: In all regs ra screen + serial | [print_hex, print_string, serial_write_hex, serial_write_string]
  - `exception_handler(regs)`: Clear screen → dump → halt | [dump_registers]
  - `isr_handler(regs)`: Dispatch tới handler hoặc `exception_handler` | []
  - `irq_handler(regs)`: Send EOI → handler | [outb]
  - `irq_remap(void)`: PIC master+slave remap (ICW1-ICW4) | [outb]
  - `register_interrupt_handler(irq, handler)`: CLI → set → STI | []
  - `init_idt(void)`: Set 256 entries → LIDT → irq_remap → STI | [idt_set_entry, irq_remap]
- **Import:** `ports.h`, `screen.h`, `serial.h`, 32 extern ISR labels + 16 extern IRQ labels (interrupt.S)

---

### `kernel/cpu/interrupt.S`

- **Vai trò:** ISR/IRQ stubs bằng GAS macros. Push số hiệu + error code, gọi C handler.
- **Macro:**
  - `ISR_NOERR num`: push 0 + push num → jmp isr_common
  - `ISR_ERR num`: push num → jmp isr_common
  - `IRQ num vec`: push 0 + push vec → jmp irq_common
- **Label:**
  - `isr_common`: pusha → push segment regs → call `isr_handler` → pop → iret
  - `irq_common`: tương tự, call `irq_handler`
- **Import:** 32 ISR + 16 IRQ global labels → gọi `isr_handler`/`irq_handler` (idt.c)

---

### `kernel/cpu/ports.h`

- **Vai trò:** inline I/O port helpers.
- **Hàm inline:**
  - `inb(port)`: đọc 1 byte | []
  - `outb(port, data)`: ghi 1 byte | []
  - `inw(port)`: đọc 1 word | []
  - `outw(port, data)`: ghi 1 word | []

---

### `kernel/cpu/timer.c` + `timer.h`

- **Vai trò:** PIT channel 0 với atomic tick counter. Hàm `sleep_ms`.
- **Hàm:**
  - `timer_get_tick()`: `lock xaddl` atomic read + increment tick_count | []
  - `timer_handler(regs)`: Gọi `timer_get_tick()` để increment | [timer_get_tick]
  - `init_timer(freq)`: Set divisor, register IRQ0 handler | [outb, register_interrupt_handler]
  - `get_ticks(void)`: Trả tick_count | []
  - `sleep_ms(ms)`: Busy-wait dựa trên tick difference | []
- **Import:** `idt.h`, `ports.h`

---

### `kernel/memory/pfa.c` + `pfa.h`

- **Vai trò:** Page Frame Allocator — bitmap-based physical memory manager.
- **Static data:** `bitmap[1024]` (8192 bits cho 32MB, 1 bit per 4KB frame)
- **Hàm:**
  - `pfa_init(void)`: Mark reserved frames (null page, kernel, stack, legacy 0xA0000-0xFFFFF) | [set_bit, serial_write_string]
  - `alloc_frame(void)`: Scan bitmap → first 0 bit → set to 1 → trả về địa chỉ physical | [test_bit, set_bit]
  - `free_frame(addr)`: Clear bit tương ứng | [clear_bit]
  - `set_bit(frame)`: Nội bộ — set bit trong bitmap | []
  - `clear_bit(frame)`: Nội bộ — clear bit | []
  - `test_bit(frame)`: Nội bộ — test bit | []
  - `mark_frame(addr)`: Nội bộ — set bit cho frame chứa addr | [set_bit]
- **Import:** `serial.h`, `bss_end` (linker symbol)

---

### `kernel/memory/paging.c` + `paging.h`

- **Vai trò:** 32-bit x86 two-level paging. Identity map 0-32MB, enable CR0.PG.
- **Static data:** `page_dir` (1024 PDEs, allocated from PFA)
- **Hàm:**
  - `init_paging(void)`: Alloc PD + first PT + PTs 4-32MB → load CR3 → set CR0.PG | [alloc_frame, create_table]
  - `create_table(virt, flags)`: Nội bộ — alloc page table, set PDE | [alloc_frame]
  - `map_page(virt, phys, flags)`: Map virtual → physical, tạo PT nếu chưa có, invlpg | [create_table]
  - `read_cr0(void)`: Trả về CR0 | []
  - `read_cr3(void)`: Trả về CR3 | []
- **Import:** `pfa.h`, `serial.h`

---

### `kernel/memory/heap.c` + `heap.h`

- **Vai trò:** Kernel heap allocator — boundary-tag first-fit malloc/free.
- **Hằng số:** `HEAP_START=0x800000`, `HEAP_SIZE=0x200000` (2MB region, identity mapped)
- **Hàm:**
  - `heap_init(void)`: Tạo 1 free block chiếm toàn bộ HEAP_SIZE | [set_footer, serial_write_string]
  - `malloc(size)`: Walk các block → first-fit → split nếu remainder >= MIN_BLOCK (12) → trả về ptr sau header | [set_footer]
  - `free(ptr)`: Mark free → merge với next block (nếu free) → merge với prev block qua boundary tag footer | [set_footer]
  - `set_footer(addr, size)`: Ghi size vào cuối block (dùng cho boundary tag) | []
- **Import:** `serial.h`
- **Bug fix:** Loại bỏ dòng `prev_addr = (unsigned int)prev_hdr;` trong backward merge (`free()`). Dòng này gán `prev_addr` bằng địa chỉ con trỏ local trên stack thay vì địa chỉ block heap, gây sai footer và heap corruption.

---

### `kernel/drivers/ata.c` + `ata.h`

- **Vai trò:** ATA PIO driver — probe primary/secondary master/slave, LBA28 read/write.
- **Static data:** `ata_exists[2][2]`, `ata_model[2][2][41]`, `ata_padding[4096]` (BSS overflow workaround)
- **Hàm:**
  - `ata_init(void)`: Probe toàn bộ 4 device (ch=0..1, dr=0..1): outb device select → outb IDENTIFY (0xEC) → poll BSY→ data → extract model string | [inb, outb, inw]
  - `ata_drive_exists(ch, dr)`: Kiểm tra `ata_exists[ch][dr]` | []
  - `ata_get_model(ch, dr)`: Trả model string | [ata_drive_exists]
  - `ata_pio(ch, dr, lba, count, buffer, write)`: LBA28 read (0x20) hoặc write (0x30). Poll BSY+DRQ, sector-by-sector | [inb, outb, outw, inw, ata_drive_exists]
  - `ata_read_sectors(ch, dr, lba, count, buffer)`: Gọi `ata_pio(write=0)` | [ata_pio]
  - `ata_write_sectors(ch, dr, lba, count, buffer)`: Gọi `ata_pio(write=1)` | [ata_pio]
- **Import:** `ports.h`, `serial.h`
- **BSS workaround:** `ata_model[2][2][41]` nằm liền kề với `sb_bitmap_start` (NVFS BSS variable). Khi ATA IDENTIFY ghi model string >40 byte, tràn vào NVFS state → zero `sb_bitmap_start` → command FAIL. Thêm `ata_padding[4096]` tạo buffer zone 4KB ngăn overflow.

---

### `kernel/drivers/nvfs.c` + `nvfs.h`

- **Vai trò:** NVFS (Noveris File System) — extent-based filesystem driver thay thế FAT16.
- **Hằng số:** `NVFS_MAGIC="NVFS"`, `NVFS_SECTOR_SIZE=512`, `NVFS_INODE_SIZE=128`, `NVFS_DIRENT_SIZE=32`, `NVFS_MAX_EXTENTS=14`, `NVFS_MAX_NAME=27`, `NVFS_ROOT_INODE=0`
- **Kiểu dữ liệu:**
  - `nvfs_extent`: {start (uint), count (uint)} — một extent liên tục
  - `nvfs_inode`: {size (uint), type (byte), reserved[3], extent_count (uint), extents[14], padding[4]}
    - type: `NVFS_TYPE_FILE=1`, `NVFS_TYPE_DIR=2`
  - `nvfs_dirent`: {name[28], inode (uint)} — directory entry
- **Static data:** `mounted`, `nvfs_ch`, `nvfs_dr`, `nvfs_cwd`, `sb_*` (superblock fields)
- **Định dạng disk:**
  - Superblock (sector 1): magic "NVFS" + all uint fields + state byte + padding
  - Block bitmap (sectors 2-9): 4096 bytes = 32768 bits cho data blocks
  - Inode table (sectors 10-41): 128 inodes × 128 bytes = 32 sectors
  - Data blocks (sectors 42-32767): 32726 blocks × 512 bytes = ~16MB
- **Hàm nội bộ (static):**
  - `find_drive(void)`: Tìm ATA device đầu tiên | [ata_drive_exists]
  - `read_sector(lba, buf)`: Đọc 1 sector từ ATA | [ata_read_sectors]
  - `write_sector(lba, buf)`: Ghi 1 sector | [ata_write_sectors]
  - `read_block(block, buf)` / `write_block(block, buf)`: Ánh xạ block number → LBA (data_start + block) | [read_sector, write_sector]
  - `bitmap_test(block)`, `bitmap_set(block)`, `bitmap_clear(block)`: Quản lý block bitmap | [read_sector, write_sector]
  - `bitmap_alloc(void)`: Scan bitmap → first free block (first-fit) | [bitmap_test, bitmap_set]
  - `inode_read(inum, inode)` / `inode_write(inum, inode)` / `inode_alloc(type)`: Quản lý inode | [read_sector, write_sector, bitmap_alloc]
  - `inode_free(inum)`: Free inode + bitmap của extents | [inode_read, inode_write, bitmap_clear]
  - `extent_read(inode, buf, max)`: Đọc nội dung file từ extents | [read_block]
  - `extent_write(inode, data, size)`: Ghi nội dung file — alloc từng extent cho mỗi lần ghi (tối đa 14 extents) | [write_block, bitmap_alloc]
  - `dir_find(parent_inum, name)`: Tìm entry trong directory | [inode_read, read_block]
  - `dir_add(parent_inum, name, child_inum)`: Thêm entry vào directory (extent mới nếu cần) | [inode_read, inode_write, read_block, write_block, bitmap_alloc]
  - `dir_remove(parent_inum, name)`: Xóa entry khỏi directory | [inode_read, inode_write, read_block, write_block]
  - `dir_empty(dir_inum)`: Kiểm tra directory rỗng | [inode_read, read_block]
  - `to_upper(s)`: uppercase | []
  - `resolve_path(path, *parent_inode, name)`: Phân tích path — hỗ trợ `/` (root), `..` (parent), `.` (current) | [dir_find, find_parent]
  - `find_parent(inum)`: Tìm parent directory bằng scan inodes tìm child_inum | [inode_read, read_block]

- **Hàm public (API):**
  - `nvfs_mount(void)`: Đọc superblock → set sb_* fields → set nvfs_cwd = root | [find_drive, read_sector]
  - `nvfs_list(path)`: List directory — `path` optional (cwd nếu rỗng). Hiển thị `[DIR]` tag + `<DIR>` cho directory, size bytes cho file | [resolve_path, inode_read, read_block]
  - `nvfs_read(path, buf, max)`: Đọc nội dung file | [resolve_path, dir_find, inode_read, extent_read]
  - `nvfs_write(path, data, size)`: Ghi file — overwrite nếu tồn tại, create nếu chưa có | [resolve_path, dir_find, inode_alloc, extent_write, dir_add, inode_write, extent_free]
  - `nvfs_delete(path)`: Xóa file — free inode + bitmap, xóa dirent | [resolve_path, dir_find, inode_read, inode_free, dir_remove]
  - `nvfs_mkdir(path)`: Tạo directory — inode loại DIR | [resolve_path, inode_alloc, dir_add]
  - `nvfs_rmdir(path)`: Xóa directory rỗng | [resolve_path, dir_find, inode_read, dir_empty, inode_free, dir_remove]
  - `nvfs_chdir(path, *out_inode)`: Đổi thư mục làm việc — hỗ trợ path rỗng (root), `..`, `.`, `./*` | [resolve_path, dir_find]
  - `nvfs_get_cwd(void)`: Trả về inode của current directory | []
  - `nvfs_path_string(inum, buf, size)`: Build path string từ root đến inum (vd `/MYDIR/SUBDIR`) | [find_parent, inode_read, read_block]

- **Import:** `nvfs.h`, `ata.h`, `screen.h`, `serial.h`

---

### `kernel/drivers/keyboard.c` + `keyboard.h`

- **Vai trò:** PS/2 keyboard driver. IRQ1 handler đọc scancode → ASCII → ring buffer.
- **Static data:** `key_buffer[256]` (ring), `shift_pressed`, `caps_on`, `extended` flag
- **Table:** `scancode_ascii[58]`, `scancode_ascii_shift[58]`
- **Hàm:**
  - `keyboard_handler(regs)`: Đọc scancode → xử lý extended (0xE0), shift, caps, → push ASCII/keycode vào ring buffer | [inb, outb]
  - `get_char(void)`: Lấy 1 ký tự từ ring buffer (non-blocking) | []
  - `read_char(void)`: Blocking get_char | [get_char]
  - `init_keyboard(void)`: Reset state, register IRQ1 handler | [register_interrupt_handler]
- **Import:** `ports.h`, `idt.h`

---

### `kernel/drivers/screen.c` + `screen.h`

- **Vai trò:** VGA text mode 80×25 driver. In ký tự, số hex, scroll, cursor.
- **Hàm:**
  - `clear_screen(void)`: Fill toàn bộ VGA memory (0xB8000) với space | [set_cursor]
  - `init_screen(void)`: Gọi clear_screen | [clear_screen]
  - `set_cursor(x, y)`: port 0x3D4/0x3D5 | [outb]
  - `scroll(void)`: Copy row 1..24 lên row 0..23, clear row 24 | []
  - `print_char(c)`: In 1 char tại cursor, xử lý \n, \b, \t | [scroll, set_cursor]
  - `print_string(str)`: In chuỗi ra serial (COM1) trước, sau đó VGA | [serial_write_string, print_char]
  - `print_hex(num)`: In 8-digit hex (0x + 8 nibbles) | [print_string]
  - `print_int(num)`: In số decimal | [print_char]
- **Import:** `ports.h`, `serial.h`

---

### `kernel/drivers/serial.c` + `serial.h`

- **Vai trò:** COM1 serial port driver — early boot logging + shell input.
- **Hàm:**
  - `init_serial(void)`: Cấu hình COM1 (baud rate, 8N1, FIFO, DTR+RTS) | [outb]
  - `is_transmit_empty(void)`: Kiểm tra line status bit 5 | [inb]
  - `serial_write_char(c)`: Chờ TX empty → outb | [is_transmit_empty, outb]
  - `serial_write_string(str)`: While loop | [serial_write_char]
  - `serial_write_hex(num)`: 8-digit hex string (0x + 8 nibbles) | [serial_write_string]
  - `serial_data_available(void)`: Kiểm tra line status bit 0 (data ready) | [inb]
  - `serial_read_char(void)`: Đọc 1 byte từ serial (non-blocking) | [inb]
- **Import:** `ports.h`
