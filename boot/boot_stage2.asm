; ── Noverix Stage 2 Bootloader ──
; Loaded at 0x7E00 by Stage 1 (MBR).
; Unlimited space: full disk read, VBE, E801, A20, GDT, 32-bit kernel load.

[org 0x7E00]
[bits 16]

%ifndef KERNEL_LBA
%define KERNEL_LBA 17
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64
%endif

KERNEL_LOAD_ADDR    equ 0x9000
KERNEL_OFFSET       equ 0x2000
BOOTDRIVE_ADDR      equ 0x7BFF

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7A00
    mov dl, [BOOTDRIVE_ADDR]
    mov [boot_drive], dl

    mov si, msg_stage2
    call print_string

    mov ax, 0x900
    mov es, ax
    xor bx, bx
    mov cx, KERNEL_SECTORS
    mov dl, [boot_drive]
    mov si, KERNEL_LBA
    cmp dl, 0x80
    jb .chs_start

    mov word [dap_sectors], 1
    mov dword [dap_lba], KERNEL_LBA
    mov word [dap_buf_seg], es
.lba_loop:
    mov word [dap_buf_off], bx
    mov ah, 0x42
    mov si, dap_start
    int 0x13
    jnc .lba_ok
    ; LBA failed — fall back to CHS
    call query_geometry
    mov si, KERNEL_LBA          ; current LBA to read
    jmp .chs
.lba_ok:
    add bx, 512
    jnc .lba_next
    mov ax, es
    add ax, 0x1000
    mov es, ax
    mov word [dap_buf_seg], es
.lba_next:
    inc dword [dap_lba]
    loop .lba_loop
    jmp .loaded

.chs_start:
    ; Floppy path: cx = KERNEL_SECTORS, si = KERNEL_LBA
    call query_geometry
    mov di, cx
    jmp .chs_begin

.chs:
    ; LBA fallback: si = current LBA, cx = remaining sectors, bx, es from LBA loop
    mov di, cx
.chs_begin:
.chs_loop:
    push es
    push bx
    ; Convert si (LBA) to CHS
    mov ax, si
    xor dx, dx
    div word [chs_sec_per_track]   ; ax = LBA/SPT (Q1), dx = LBA%SPT (R1)
    mov cl, dl
    inc cl                         ; cl = sector (1-based), 6 bits
    xor dx, dx
    div word [chs_heads]           ; ax = cylinder (10 bits), dx = head
    mov dh, dl                     ; dh = head
    ; Build CL: low 6 bits = sector, high 2 bits = cylinder bits 9-8
    push ax
    mov al, ah
    and al, 0x03
    shl al, 6
    or cl, al
    pop ax
    mov ch, al                     ; ch = cylinder low 8 bits
    pop bx
    pop es
    mov ax, 0x0201
    int 0x13
    jc disk_error
    add bx, 512
    jnc .chs_next
    mov ax, es
    add ax, 0x1000
    mov es, ax
.chs_next:
    inc si
    dec di
    jnz .chs_loop
    jmp .loaded

.loaded:
    mov si, msg_kernel_loaded
    call print_string

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
    mov dword [0x1004], 0
    mov dword [0x1008], 0
    mov byte  [0x100A], 0
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

[bits 16]
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
    cmp al, 0xFF
    je .no_port
    test al, 2
    jnz .no_port
    or al, 2
    out 0x92, al
.no_port:
    mov ax, 0x2401
    int 0x15
    popa
    ret

; Query drive geometry via INT 13h AH=08h.
; Stores heads and sectors-per-track in chs_heads / chs_sec_per_track.
query_geometry:
    pusha
    mov dl, [boot_drive]
    mov ah, 0x08
    int 0x13
    jc .qd_default
    and cx, 0x3F                ; CL bits 5-0 = max sector number
    mov [chs_sec_per_track], cx
    movzx ax, dh
    inc ax
    mov [chs_heads], ax
    popa
    ret
.qd_default:
    ; Fallback: 1.44 MB floppy geometry (18 spt, 2 heads)
    mov word [chs_sec_per_track], 18
    mov word [chs_heads], 2
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

boot_drive      db 0
chs_sec_per_track dw 18
chs_heads       dw 2
dap_start:
dap_size:       db 0x10
dap_reserved:   db 0
dap_sectors:    dw 0
dap_buf_off:    dw 0
dap_buf_seg:    dw 0
dap_lba:        dd 0, 0
msg_stage2      db "Noverix Stage 2", 0x0D, 0x0A, 0
msg_kernel_loaded db "Kernel loaded", 0x0D, 0x0A, 0
msg_disk_error  db "Disk!", 0

; Stage 2 must fit below kernel load address (0x9000 - 0x7E00 = 0x1200)
%if ($ - $$) > 0x1200
%error "Stage 2 bootloader exceeds 0x1200 bytes — overlaps kernel load area"
%endif
