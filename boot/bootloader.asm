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
    mov sp, 0x7C00 - 512
    mov [boot_drive], dl

    mov ax, 0x900
    mov es, ax
    xor bx, bx
    mov cx, KERNEL_SECTORS

    mov dl, [boot_drive]
    cmp dl, 0x80
    jb .chs

    mov word [dap_sectors], 1
    mov word [dap_lba], 1
    mov word [dap_buf_seg], es
.lba_loop:
    mov word [dap_buf_off], bx
    mov ah, 0x42
    mov si, dap_start
    int 0x13
    jc disk_error
    add bx, 512
    jnc .lba_next
    mov ax, es
    add ax, 0x1000
    mov es, ax
    mov word [dap_buf_seg], es
.lba_next:
    add word [dap_lba], 1
    loop .lba_loop
    jmp .loaded

.chs:
    push cx
    pop si
    mov cl, 2
    xor dh, dh
    xor ch, ch
.chs_loop:
    mov ax, 0x0201
    int 0x13
    jc disk_error
    add bx, 512
    jnc .chs_next
    mov ax, es
    add ax, 0x1000
    mov es, ax
.chs_next:
    inc cl
    cmp cl, 19
    jb .chs_track
    mov cl, 1
    inc dh
    cmp dh, 2
    jb .chs_track
    xor dh, dh
    inc ch
.chs_track:
    dec si
    jnz .chs_loop

.loaded:
    xor ax, ax
    mov es, ax
    mov ax, 0x4F01
    mov cx, 0x0115
    mov di, 0x0600
    int 0x10
    cmp al, 0x4F
    jne .vbe_fail
    mov ax, 0x4F02
    mov bx, 0x4115
    int 0x10
    cmp al, 0x4F
    jne .vbe_fail
    mov eax, [es:di + 40]
    mov [0x1000], eax
    mov ax, [es:di + 18]
    mov [0x1004], ax
    mov ax, [es:di + 20]
    mov [0x1006], ax
    mov ax, [es:di + 16]
    mov [0x1008], ax
    mov al, [es:di + 25]
    mov [0x100A], al
    jmp .vbe_done
.vbe_fail:
    mov dword [0x1000], 0
.vbe_done:

    mov ax, 0xE801
    int 0x15
    jc .detect_default
    cmp ah, 0x80
    jae .detect_default
    movzx ecx, cx
    shl ecx, 10
    movzx edx, dx
    shl edx, 16
    lea eax, [ecx + edx]
    add eax, 0x100000
    mov [0x100C], eax
    jmp .detect_done
.detect_default:
    mov dword [0x100C], 32 * 1024 * 1024
.detect_done:

    xor ax, ax
    mov es, ax
    mov si, pm_trampoline
    mov di, PM_TRAMPOLINE_ADDR
    mov cx, pm_trampoline_end - pm_trampoline
    cld
    rep movsb
    jmp switch_to_pm

; ── 32-bit protected mode trampoline (copied to 0x500, never executed in place) ──
[bits 32]
pm_trampoline:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000
    mov esi, KERNEL_LOAD_ADDR
    mov edi, KERNEL_OFFSET
    mov ecx, KERNEL_SECTORS * 128
    cld
    rep movsd
    mov eax, KERNEL_OFFSET
    call eax
    jmp $
pm_trampoline_end:

; ── 16-bit helper functions (never executed after PM entry) ──
[bits 16]
disk_error:
    jmp $

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

switch_to_pm:
    cli
    lgdt [gdt_descriptor]
    in al, 0x92
    test al, 2
    jnz .a20_on
    or al, 2
    out 0x92, al
    mov ax, 0x2401
    int 0x15
.a20_on:
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp CODE_SEG:PM_TRAMPOLINE_ADDR

boot_drive      db 0
dap_start:
dap_size:       db 0x10
dap_reserved:   db 0
dap_sectors:    dw 0
dap_buf_off:    dw 0
dap_buf_seg:    dw 0
dap_lba:        dd 0, 0

; ── MBR Partition Table (VMware/HW BIOS requires at least one entry for HDD boot) ──
times 446 - ($ - $$) db 0
db 0x00
db 0x00, 0x02, 0x00
db 0x83
db 0xFE, 0xFF, 0xFF
dd 0x00000001
dd 0xFFFFFFFF
times 48 db 0
dw 0xAA55
