[org 0x7C00]
[bits 16]

KERNEL_OFFSET  equ 0x2000
KERNEL_LOAD_ADDR equ 0x0600

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 64
%endif

start:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x9000
    mov [boot_drive], dl

    mov si, msg_loading
    call print_string

    mov bx, KERNEL_LOAD_ADDR
    mov cx, KERNEL_SECTORS
    mov dl, [boot_drive]
    call disk_load

    ; Copy PM trampoline to safe address below kernel range
    ; so the rep movsd that copies kernel data won't overwrite its own code
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

disk_load:
    pusha

    mov [disk_drive], dl
    mov [disk_sectors], cx
    mov [disk_buffer], bx
    mov byte [sector], 2
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
    or al, 0x02
    out 0x92, al

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
    mov esp, 0x9000

    ; Backwards copy: source (0x0600) < dest (0x2000) with overlap.
    ; Starting at the top ensures source data is read before being overwritten.
    mov esi, KERNEL_LOAD_ADDR + KERNEL_SECTORS * 512 - 4
    mov edi, KERNEL_OFFSET + KERNEL_SECTORS * 512 - 4
    mov ecx, KERNEL_SECTORS * 128
    std
    rep movsd
    cld

    mov eax, KERNEL_OFFSET
    call eax
    jmp $
pm_trampoline_end:

boot_drive  db 0
msg_loading db "Noveris OS - Loading kernel...", 13, 10, 0
msg_disk_error db "Disk error! Halting.", 13, 10, 0

disk_buffer dw 0
disk_sectors dw 0
disk_drive  db 0
sector      db 2
head        db 0
cylinder    db 0

times 510 - ($ - $$) db 0
dw 0xAA55
