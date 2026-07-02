.extern kernel_main
.extern bss_start
.extern bss_end
.globl _start
_start:
    cld
    mov $bss_start, %edi
    mov $bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    shr $2, %ecx
    rep stosl
    jmp kernel_main