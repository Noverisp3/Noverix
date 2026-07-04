#include "acpi.h"
#include "../drivers/serial.h"

/* Search a memory region for the RSDP signature "RSD PTR " */
static const void *rsdp_search_region(const void *start, unsigned int length)
{
    const unsigned char *p = (const unsigned char *)start;
    const unsigned char *end = p + length;
    while (p + 20 <= end) {
        if (p[0] == 'R' && p[1] == 'S' && p[2] == 'D' && p[3] == ' '
            && p[4] == 'P' && p[5] == 'T' && p[6] == 'R' && p[7] == ' ') {
            /* Verify checksum: first 20 bytes sum to 0 */
            unsigned char sum = 0;
            for (int i = 0; i < 20; i++)
                sum += p[i];
            if (sum == 0)
                return p;
        }
        p += 16;  /* RSDP is 16-byte aligned */
    }
    return 0;
}

const void *acpi_find_rsdp(void)
{
    /* Try EBDA: word at 0x040E = segment address of EBDA */
    unsigned short ebda_seg = *(volatile unsigned short *)0x040E;
    if (ebda_seg) {
        const void *rsdp = rsdp_search_region(
            (const void *)(unsigned int)(ebda_seg * 16), 1024);
        if (rsdp)
            return rsdp;
    }

    /* Search BIOS ROM area 0x000E0000 – 0x000FFFFF */
    return rsdp_search_region((const void *)0x000E0000, 0x00020000);
}
