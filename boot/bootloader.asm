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

    mov si, msg_loading
    call print_string

    mov ax, 0x900
    mov es, ax
    xor bx, bx
    mov cx, KERNEL_SECTORS

    mov dl, [boot_drive]
    cmp dl, 0x80
    jb .chs

    mov word [dap_sectors], cx
    mov word [dap_buf_off], bx
    mov word [dap_buf_seg], es
    mov dword [dap_lba], 1
    mov dword [dap_lba + 4], 0
    mov ah, 0x42
    mov si, dap_start
    int 0x13
    jc disk_error
    jmp .loaded

.chs:
    mov [disk_sectors], cx
    mov byte [sector], 2
    mov byte [head], 0
    mov byte [cylinder], 0
.next_chs:
    mov ah, 0x02
    mov al, 1
    mov ch, [cylinder]
    mov dh, [head]
    mov cl, [sector]
    mov bx, [disk_buffer]
    int 0x13
    jc disk_error
    add word [disk_buffer], 512
    jnc .sec_ok
    mov ax, es
    add ax, 0x1000
    mov es, ax
.sec_ok:
    dec word [disk_sectors]
    jz .loaded
    inc byte [sector]
    cmp byte [sector], 19
    jb .next_chs
    mov byte [sector], 1
    inc byte [head]
    cmp byte [head], 2
    jb .next_chs
    mov byte [head], 0
    inc byte [cylinder]
    jmp .next_chs

.loaded:
    mov si, msg_loaded
    call print_string

    ; ── VBE init: try 800x600x24 (mode 0x115) ──
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

    xor ax, ax
    mov es, ax
    mov si, pm_trampoline
    mov di, PM_TRAMPOLINE_ADDR
    mov cx, pm_trampoline_end - pm_trampoline
    cld
    rep movsb
    jmp switch_to_pm

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

disk_error:
    mov si, msg_disk_error
    call print_string
    jmp $

enable_a20:
    pusha
    in al, 0x92
    test al, 2
    jnz .a20_on
    or al, 2
    out 0x92, al
    mov ax, 0x2401
    int 0x15
.a20_on:
    popa
    ret

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
    call enable_a20
    mov eax, cr0
    or eax, 0x01
    mov cr0, eax
    jmp CODE_SEG:PM_TRAMPOLINE_ADDR

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

boot_drive      db 0
dap_start:
dap_size:       db 0x10
dap_reserved:   db 0
dap_sectors:    dw 0
dap_buf_off:    dw 0
dap_buf_seg:    dw 0
dap_lba:        dd 0, 0
disk_buffer     dw 0
disk_sectors    dw 0
sector          db 2
head            db 0
cylinder        db 0
msg_loading     db "Noverix Loading...", 13, 10, 0
msg_loaded      db "Loaded.", 13, 10, 0
msg_disk_error  db "Disk error!", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
