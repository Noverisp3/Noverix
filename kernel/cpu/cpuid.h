#ifndef CPUID_H
#define CPUID_H

static inline void cpuid(unsigned int eax, unsigned int ecx,
                         unsigned int *eax_out, unsigned int *ebx_out,
                         unsigned int *ecx_out, unsigned int *edx_out)
{
    unsigned int a, b, c, d;
    __asm__ volatile ("cpuid"
        : "=a" (a), "=b" (b), "=c" (c), "=d" (d)
        : "a" (eax), "c" (ecx)
    );
    if (eax_out) *eax_out = a;
    if (ebx_out) *ebx_out = b;
    if (ecx_out) *ecx_out = c;
    if (edx_out) *edx_out = d;
}

#endif
