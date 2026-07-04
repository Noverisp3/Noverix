; AP Trampoline — 16-bit real mode → 32-bit protected mode with paging
;
; Placed at 0x70000 by BSP before SIPI. Each AP starts here.
; Data area at 0x70200: BSP writes CR3 + ap_main address there.
; Vector 0x70 → AP starts at CS=0x7000, IP=0x0000 → phys 0x70000.

[org 0x70000]
[bits 16]

ap_start:
    cli

    ; SIPI sets CS = 0x7000 for start at 0x70000.
    ; Set DS/ES/SS to match so [gdtr] resolves correctly with org 0x70000.
    mov ax, 0x7000
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor sp, sp

    ; Enable A20 via port 0x92
    in al, 0x92
    or al, 0x02
    out 0x92, al

    ; Load temporary GDT
    lgdt [gdtr]

    ; Switch to protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump to 32-bit code
    jmp dword 0x08:ap_32

[bits 32]
ap_32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Use stack at top of trampoline page (grows down)
    mov esp, 0x70E00

    ; Enable paging: load CR3 from data area, set PG bit
    mov eax, [0x70200]
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    ; Read LAPIC ID
    mov eax, [0xFEE00020]
    shr eax, 24

    ; Call ap_main(apic_id) from kernel
    push eax
    mov eax, [0x70204]
    call eax
    add esp, 4

    ; Should not return
ap_halt:
    cli
    hlt
    jmp ap_halt

; ── Temporary GDT (null + code32 + data32) ──
align 4
gdt_start:
    dq 0                                    ; null
gdt_code:
    dw 0xFFFF                               ; limit[15:0]
    dw 0                                    ; base[15:0]
    db 0                                    ; base[23:16]
    db 0x9A                                 ; access (code, ring0)
    db 0xCF                                 ; gran + limit[19:16]
    db 0                                    ; base[31:24]
gdt_data:
    dw 0xFFFF                               ; limit[15:0]
    dw 0                                    ; base[15:0]
    db 0                                    ; base[23:16]
    db 0x92                                 ; access (data, ring0)
    db 0xCF                                 ; gran + limit[19:16]
    db 0                                    ; base[31:24]
gdt_end:

gdtr:
    dw gdt_end - gdt_start - 1
    dd gdt_start
