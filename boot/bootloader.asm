; ── Noverix Stage 1 MBR ──
; Single job: load Stage 2 from LBA 1-16 into 0x7E00, then jump to it.
; Must fit in 446 bytes (partition table occupies 446-509).

[org 0x7C00]
[bits 16]

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 16
%endif

STAGE2_ADDR     equ 0x7E00
BOOTDRIVE_ADDR  equ 0x7BFF

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7A00
    mov [BOOTDRIVE_ADDR], dl

    mov si, dap
    mov ah, 0x42
    int 0x13
    jc .chs
    jmp .done

.chs:
    mov ax, 0x0200 | STAGE2_SECTORS
    mov cx, 0x0002
    xor dh, dh
    xor bx, bx
    mov es, bx
    mov bx, STAGE2_ADDR
    int 0x13
    jc disk_error

.done:
    jmp 0x0000:STAGE2_ADDR

disk_error:
    jmp $

dap:
    db 0x10, 0
    dw STAGE2_SECTORS
    dw STAGE2_ADDR
    dw 0
    dd 1, 0

; ── MBR Partition Table ──
times 446 - ($ - $$) db 0
db 0x00
db 0x00, 0x02, 0x00
db 0x83
db 0xFE, 0xFF, 0xFF
dd 0x00000001
dd 0xFFFFFFFF
times 48 db 0
dw 0xAA55
