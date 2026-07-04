#include "acpi.h"
#include "../drivers/serial.h"
#include "../memory/paging.h"

int cpu_count;
cpu_info_t cpu_info[MAX_CPU];
static unsigned int lapic_addr;

static int acpi_checksum(const void *data, unsigned int length)
{
    unsigned char sum = 0;
    for (unsigned int i = 0; i < length; i++)
        sum += ((const unsigned char *)data)[i];
    return sum == 0;
}

/* Identity-map a physical memory region so it's accessible after paging is on */
static void identity_map_region(unsigned int phys, unsigned int size)
{
    unsigned int start = phys & ~0xFFF;
    unsigned int end = ((phys + size + 0xFFF) & ~0xFFF);
    for (unsigned int p = start; p < end; p += 0x1000)
        map_page(p, p, PAGE_WRITE);
}

/* Map and return a validated pointer to a physical address with known minimum size */
static void *map_access(unsigned int phys, unsigned int min_size)
{
    identity_map_region(phys, min_size);
    return (void *)phys;
}

int acpi_parse_madt(void)
{
    const rsdp_t *rsdp = (const rsdp_t *)acpi_find_rsdp();
    if (!rsdp) {
        serial_write_string("[acpi] RSDP not found\n");
        return -1;
    }
    serial_write_string("[acpi] RSDP at ");
    serial_write_hex((unsigned int)rsdp);
    serial_write_char('\n');

    unsigned int rsdt_phys = rsdp->rsdt_address;
    serial_write_string("[acpi] RSDT phys ");
    serial_write_hex(rsdt_phys);
    serial_write_char('\n');

    /* Map and read RSDT header to get total length */
    const sdt_header_t *rsdt_hdr = (const sdt_header_t *)map_access(rsdt_phys, sizeof(sdt_header_t));
    unsigned int rsdt_len = rsdt_hdr->length;
    if (rsdt_len < sizeof(sdt_header_t)) {
        serial_write_string("[acpi] RSDT too small\n");
        return -1;
    }
    /* Map the full RSDT */
    const rsdt_t *rsdt = (const rsdt_t *)map_access(rsdt_phys, rsdt_len);
    if (!acpi_checksum(rsdt, rsdt_len)) {
        serial_write_string("[acpi] RSDT checksum FAIL\n");
        return -1;
    }
    serial_write_string("[acpi] RSDT len=");
    serial_write_int(rsdt_len);
    serial_write_string(" entries=");
    unsigned int entry_count = (rsdt_len - sizeof(sdt_header_t)) / 4;
    serial_write_int(entry_count);
    serial_write_char('\n');

    /* Walk RSDT entries looking for MADT (signature "APIC") */
    const madt_t *madt = 0;
    for (unsigned int i = 0; i < entry_count; i++) {
        unsigned int entry_phys = rsdt->entry[i];
        const sdt_header_t *hdr = (const sdt_header_t *)map_access(entry_phys, sizeof(sdt_header_t));
        unsigned int hdr_len = hdr->length;
        if (hdr_len < sizeof(sdt_header_t)) continue;
        hdr = (const sdt_header_t *)map_access(entry_phys, hdr_len);
        if (!acpi_checksum(hdr, hdr_len)) {
            serial_write_string("[acpi] table checksum FAIL at ");
            serial_write_hex(entry_phys);
            serial_write_char('\n');
            continue;
        }
        if (hdr->signature[0] == 'A' && hdr->signature[1] == 'P'
            && hdr->signature[2] == 'I' && hdr->signature[3] == 'C') {
            madt = (const madt_t *)hdr;
            break;
        }
    }
    if (!madt) {
        serial_write_string("[acpi] MADT not found\n");
        return -1;
    }
    serial_write_string("[acpi] MADT at ");
    serial_write_hex((unsigned int)madt);
    serial_write_char('\n');

    lapic_addr = madt->local_apic_address;
    serial_write_string("[acpi] LAPIC at ");
    serial_write_hex(lapic_addr);
    serial_write_char('\n');

    /* Parse MADT entries */
    cpu_count = 0;
    const unsigned char *p = madt->entry;
    const unsigned char *end = (const unsigned char *)madt + madt->header.length;
    while (p < end) {
        unsigned char type = p[0];
        unsigned char len = p[1];
        if (len < 2) break;
        if (type == MADT_TYPE_PROCESSOR && len >= 8) {
            const madt_processor_t *proc = (const madt_processor_t *)p;
            if (proc->flags & 1) {
                if (cpu_count < MAX_CPU) {
                    cpu_info[cpu_count].apic_id = proc->apic_id;
                    cpu_info[cpu_count].state = CPU_UNINITIALIZED;
                    cpu_info[cpu_count].stack_top = 0;
                    cpu_count++;
                }
            }
        } else if (type == MADT_TYPE_IOAPIC && len >= 12) {
            const madt_ioapic_t *ioapic = (const madt_ioapic_t *)p;
            serial_write_string("[acpi] I/O APIC at ");
            serial_write_hex(ioapic->address);
            serial_write_string(" GSI base=");
            serial_write_int(ioapic->global_system_interrupt_base);
            serial_write_char('\n');
        }
        p += len;
    }

    serial_write_string("[acpi] found ");
    serial_write_int(cpu_count);
    serial_write_string(" CPU(s)\n");
    for (int i = 0; i < cpu_count; i++) {
        serial_write_string("  CPU ");
        serial_write_int(i);
        serial_write_string(" APIC ID ");
        serial_write_int(cpu_info[i].apic_id);
        serial_write_char('\n');
    }
    return cpu_count;
}

unsigned int acpi_get_lapic_addr(void)
{
    return lapic_addr;
}
