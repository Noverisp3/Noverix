# CODEBASE INDEX — Noveris OS

## 1. KIẾN TRÚC TỔNG QUAN & LUỒNG DỮ LIỆU

**Mô hình:** Monolithic Kernel — single binary loaded at 0x2000, no user/kernel space separation, bare-metal x86.

```
BIOS
 └─ boot/bootloader.asm (real-mode INT 0x13 load kernel → 0x0600)
    └─ PM trampoline (copy 0x0600 → 0x2000)
       └─ kernel/entry.S (BSS zeroing)
          └─ kernel/kernel.c::kernel_main (C entry)
             ├─ kernel/cpu/gdt.c       → GDT reload
             ├─ kernel/cpu/idt.c       → IDT + PIC remap + exceptions
             ├─ kernel/drivers/screen.c → VGA text mode
             ├─ kernel/drivers/keyboard.c → PS/2 IRQ1
             ├─ kernel/cpu/timer.c     → PIT IRQ0
             ├─ kernel/drivers/ata.c   → ATA probe
             ├─ kernel/drivers/fat16.c → mount FAT16 from ATA LBA
             └─ while(1): readline → handle_cmd → dispatch
```

**Luồng dữ liệu disk:** ATA PIO LBA28 → raw sector buffer → FAT16 BPB parsing → root dir / FAT chain → file data. Ghi: memory buffer → cluster allocation → FAT update (2 copies) → data write → dir entry update.

## 2. SƠ ĐỒ CẤU TRÚC THƯ MỤC

```
Project_002_OS/
├── boot/
│   └── bootloader.asm        # MBR: real-mode → A20 → GDT → PM trampoline
├── kernel/
│   ├── entry.S               # Entry: BSS zero → jmp kernel_main
│   ├── kernel.c              # Shell: readline, history, cmd dispatch
│   ├── cpu/
│   │   ├── gdt.c / gdt.h     # GDT (null,code,data,user_code,user_data)
│   │   ├── idt.c / idt.h     # IDT (32 ISR + 16 IRQ), register dump
│   │   ├── interrupt.S       # ISR/IRQ stubs, common handler asm
│   │   ├── ports.h           # inb/outb/inw/outw inline asm
│   │   ├── timer.c / timer.h # PIT channel 0, atomic tick, sleep_ms
│   └── drivers/
│       ├── ata.c / ata.h     # ATA PIO: probe, LBA28 read/write
│       ├── fat16.c / fat16.h # FAT16: mount, list, read, write, delete
│       ├── keyboard.c / .h   # PS/2 IRQ1: scancode→ASCII, ring buf
│       ├── screen.c / .h     # VGA 80×25: print, scroll, cursor
│       └── serial.c / .h    # COM1: init, putchar, puts, puthex
├── linker.ld                 # ELF linker: 0x2000, PHDRS RX/RW
├── Makefile                  # clang + nasm + ld.bfd + objcopy
├── disk.img                  # 16MB FAT16 raw image
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
  - `pm_trampoline` (32-bit): reload segments, ESP=0x90000, backwards rep movsd 0x9000→0x2000, call 0x2000 | []
- **Import:** Constants `KERNEL_OFFSET=0x2000`, `KERNEL_LOAD_ADDR=0x9000`, `PM_TRAMPOLINE_ADDR=0x0500`

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
  - `readline(buf, max)`: Đọc input từ keyboard, inline editing (LEFT/RIGHT di chuyển, insert/delete giữa dòng), UP/DOWN history | [read_char, print_string, history_add]
  - `handle_cmd(buf)`: Parse cmd/arg, dispatch | [strcmp, print_string, clear_screen, print_hex, sleep_ms, ata_drive_exists, ata_get_model, fat_read, fat_list, fat_write, fat_delete, reboot, shutdown]
  - `kernel_main(void)`: Init sequence → shell loop | [init_serial, init_gdt, init_idt, init_screen, init_keyboard, init_timer, ata_init, fat_mount]
- **Import:** `screen.h`, `keyboard.h`, `serial.h`, `ata.h`, `fat16.h`, `gdt.h`, `idt.h`, `timer.h`, `ports.h`

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

### `kernel/drivers/ata.c` + `ata.h`

- **Vai trò:** ATA PIO driver — probe primary/secondary master/slave, LBA28 read/write.
- **Static data:** `ata_exists[2][2]`, `ata_model[2][2][41]`
- **Hàm:**
  - `ata_init(void)`: Probe toàn bộ 4 device (ch=0..1, dr=0..1): outb device select → outb IDENTIFY (0xEC) → poll BSY→ data → extract model string | [inb, outb, inw]
  - `ata_drive_exists(ch, dr)`: Kiểm tra `ata_exists[ch][dr]` | []
  - `ata_get_model(ch, dr)`: Trả model string | [ata_drive_exists]
  - `ata_pio(ch, dr, lba, count, buffer, write)`: LBA28 read (0x20) hoặc write (0x30). Poll BSY+DRQ, sector-by-sector | [inb, outb, outw, inw, ata_drive_exists]
  - `ata_read_sectors(ch, dr, lba, count, buffer)`: Gọi `ata_pio(write=0)` | [ata_pio]
  - `ata_write_sectors(ch, dr, lba, count, buffer)`: Gọi `ata_pio(write=1)` | [ata_pio]
- **Import:** `ports.h`, `serial.h`

---

### `kernel/drivers/fat16.c` + `fat16.h`

- **Vai trò:** FAT16 filesystem driver — BPB parsing, root dir, cluster chain, file CRUD.
- **Static data:** BPB fields, `fat_ch`, `fat_dr` (`buf[512]` là local stack variable trong mỗi hàm để tránh race condition)
- **Hàm:**
  - `find_first_drive(void)`: Tìm ATA device đầu tiên (ch 0..1, dr 0..1) | [ata_drive_exists]
  - `to_83(name, out)`: Chuyển "file.txt" → "FILE    TXT" | []
  - `read_sector(lba, dst)`: Đọc 1 sector từ ATA | [ata_read_sectors]
  - `write_sector_raw(lba, src)`: Ghi 1 sector | [ata_write_sectors]
  - `fat_mount(void)`: Đọc BPB sector 0 → parse bytes_per_sector, spc, reserved, fats, root_entries, spf → tính root_start, data_start | [read_sector]
  - `find_entry(name83, *out_off, *out_cluster, *out_size)`: Scan root dir → match 11-byte name → trả sector index + offset + cluster + size | [read_sector]
  - `next_cluster(cluster)`: Đọc FAT entry 16-bit | [read_sector]
  - `fat_list(void)`: Scan root dir → in tên + size + dir flag | [read_sector, print_string, print_hex]
  - `fat_read(name, out, max)`: Tra cứu → walk cluster chain → copy data | [find_entry, read_sector, next_cluster]
  - `fat_write(name, data, size)`: Xóa cluster cũ nếu tồn tại → tìm free dir entry → allocate cluster chain (FAT1 + FAT2 mirror) → write data → update dir entry | [find_entry, read_sector, write_sector_raw, next_cluster]
  - `fat_delete(name)`: Tìm entry → free cluster chain (FAT1 + FAT2) → mark dir entry 0xE5 | [find_entry, read_sector, write_sector_raw, next_cluster]
- **Import:** `ata.h`, `screen.h`, `ports.h`, `serial.h`

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
  - `print_string(str)`: In chuỗi | [print_char]
  - `print_hex(num)`: In 8-digit hex (0x + 8 nibbles) | [print_string]
  - `print_int(num)`: In số decimal | [print_char]
- **Import:** `ports.h`

---

### `kernel/drivers/serial.c` + `serial.h`

- **Vai trò:** COM1 serial port driver — early boot logging.
- **Hàm:**
  - `init_serial(void)`: Cấu hình COM1 (baud rate, 8N1, FIFO, DTR+RTS) | [outb]
  - `is_transmit_empty(void)`: Kiểm tra line status bit 5 | [inb]
  - `serial_write_char(c)`: Chờ TX empty → outb | [is_transmit_empty, outb]
  - `serial_write_string(str)`: While loop | [serial_write_char]
  - `serial_write_hex(num)`: 8-digit hex string (0x + 8 nibbles) | [serial_write_string]
- **Import:** `ports.h`
