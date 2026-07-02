[org 0x7C00]
[bits 16]

KERNEL_OFFSET      equ 0x2000
KERNEL_LOAD_ADDR   equ 0x9000

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64
%endif

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00 - 512          ; safe stack, avoid IVT/BDA
    mov [boot_drive], dl

    mov si, msg_loading
    call print_string

    ; Load kernel to linear address 0x9000 (ES=0x900, BX=0)
    ; ES segment avoids 16-bit BX overflow when kernel > 56 sectors
    mov ax, 0x900
    mov es, ax
    xor bx, bx
    mov cx, KERNEL_SECTORS
    mov dl, [boot_drive]
    call disk_load

    ; Print success message
    mov si, msg_loaded
    call print_string

    ; Restore ES=0 (rep movsb uses ES:DI, and we changed ES for disk load)
    xor ax, ax
    mov es, ax

    ; Copy trampoline code to safe area (0x0500)
    mov si, pm_trampoline
    mov di, PM_TRAMPOLINE_ADDR
    mov cx, pm_trampoline_end - pm_trampoline
    cld
    rep movsb

    jmp switch_to_pm

; Print string function (BIOS teletype)
print_string:
    push ax
    push bx
    mov ah, 0x0E
    xor bx, bx
.loop:
    lodsb
    or al, al
    jz .done
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

; Disk read using CHS (supports up to 1.44MB)
disk_load:
    pusha

    mov [disk_drive], dl
    mov [disk_sectors], cx
    mov [disk_buffer], bx
    mov byte [sector], 2        ; starting sector (sector 1 is boot sector)
    mov byte [head], 0
    mov byte [cylinder], 0

.next:
    mov ah, 0x02
    mov al, 1
    mov ch, [cylinder]
    mov dh, [head]
    mov cl, [sector]
    mov dl, [disk_drive]
    mov bx, [disk_buffer]
    int 0x13
    jc .error

    add word [disk_buffer], 512
    jnc .sector_ok
    mov ax, es
    add ax, 0x1000
    mov es, ax
.sector_ok:
    dec word [disk_sectors]
    jz .done

    inc byte [sector]
    cmp byte [sector], 19
    jb .next
    mov byte [sector], 1
    inc byte [head]
    cmp byte [head], 2
    jb .next
    mov byte [head], 0
    inc byte [cylinder]
    jmp .next

.done:
    popa
    ret

.error:
    mov si, msg_disk_error
    call print_string
    jmp $

; Enable A20 using multiple methods
enable_a20:
    pusha
    ; Method 1: port 0x92 (fast, used on newer motherboards)
    in al, 0x92
    test al, 2
    jnz .a20_on
    or al, 2
    out 0x92, al
    ; verify again
    in al, 0x92
    test al, 2
    jnz .a20_on

    ; Method 2: Keyboard controller (ports 0x64, 0x60)
    call .wait_kbc_write
    mov al, 0xD1        ; command write output port
    out 0x64, al
    call .wait_kbc_write
    mov al, 0xDF        ; bit 2 = 1 (enable A20)
    out 0x60, al
    call .wait_kbc_write

    ; Method 3: BIOS int 0x15 (if available)
    mov ax, 0x2401
    int 0x15

.a20_on:
    popa
    ret

.wait_kbc_write:
    in al, 0x64
    test al, 2
    jnz .wait_kbc_write
    ret

; 32-bit GDT (flat)
gdt_start:
    dq 0x0000000000000000
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

PM_TRAMPOLINE_ADDR equ 0x0500

; Switch to protected mode
switch_to_pm:
    cli
    lgdt [gdt_descriptor]
    call enable_a20          ; enable A20 using multiple methods
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp CODE_SEG:PM_TRAMPOLINE_ADDR

; Trampoline (runs in protected mode)
[bits 32]
pm_trampoline:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000

    ; Copy kernel from KERNEL_LOAD_ADDR (0x9000) to KERNEL_OFFSET (0x2000)
    ; Use backwards copy to avoid overwriting when destination overlaps
    mov esi, KERNEL_LOAD_ADDR + KERNEL_SECTORS * 512 - 4
    mov edi, KERNEL_OFFSET + KERNEL_SECTORS * 512 - 4
    mov ecx, KERNEL_SECTORS * 128      ; 512 bytes / 4 = 128 dwords per sector
    std
    rep movsd
    cld

    ; Jump into kernel
    mov eax, KERNEL_OFFSET
    call eax
    jmp $
pm_trampoline_end:

boot_drive      db 0
msg_loading     db "Noveris OS - Loading kernel...", 13, 10, 0
msg_loaded      db "Kernel loaded successfully.", 13, 10, 0
msg_disk_error  db "Disk error! Halting.", 13, 10, 0

disk_buffer     dw 0
disk_sectors    dw 0
disk_drive      db 0
sector          db 2
head            db 0
cylinder        db 0

times 510 - ($ - $$) db 0
dw 0xAA55