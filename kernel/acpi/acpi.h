#ifndef ACPI_H
#define ACPI_H

#include "../cpu/cpu.h"

/* RSDP — Root System Description Pointer (ACPI v1, 20 bytes) */
typedef struct {
    char signature[8];          /* "RSD PTR " */
    unsigned char checksum;
    char oem_id[6];
    unsigned char revision;      /* 0=v1, 2=v2+ */
    unsigned int rsdt_address;
} __attribute__((packed)) rsdp_t;

/* SDT header — System Description Table */
typedef struct {
    char signature[4];
    unsigned int length;
    unsigned char revision;
    unsigned char checksum;
    char oem_id[6];
    char oem_table_id[8];
    unsigned int oem_revision;
    unsigned int creator_id;
    unsigned int creator_revision;
} __attribute__((packed)) sdt_header_t;

/* RSDT — Root System Description Table */
typedef struct {
    sdt_header_t header;
    unsigned int entry[1];       /* variable-length array of 32-bit pointers */
} __attribute__((packed)) rsdt_t;

/* MADT — Multiple APIC Description Table */
typedef struct {
    sdt_header_t header;
    unsigned int local_apic_address;
    unsigned int flags;
    unsigned char entry[1];      /* variable-length entries */
} __attribute__((packed)) madt_t;

/* MADT entry types */
#define MADT_TYPE_PROCESSOR     0
#define MADT_TYPE_IOAPIC        1
#define MADT_TYPE_ISO           2
#define MADT_TYPE_NMI           4

/* MADT entry: Processor Local APIC */
typedef struct {
    unsigned char type;          /* 0 */
    unsigned char length;        /* 8 */
    unsigned char acpi_processor_id;
    unsigned char apic_id;
    unsigned int flags;          /* bit 0 = enabled */
} __attribute__((packed)) madt_processor_t;

/* MADT entry: I/O APIC */
typedef struct {
    unsigned char type;          /* 1 */
    unsigned char length;        /* 12 */
    unsigned char ioapic_id;
    unsigned char reserved;
    unsigned int address;
    unsigned int global_system_interrupt_base;
} __attribute__((packed)) madt_ioapic_t;

/* Returns number of CPUs found, populates cpu_info[] */
int acpi_parse_madt(void);

/* Returns LAPIC base address */
unsigned int acpi_get_lapic_addr(void);

/* Find RSDP pointer (internal, used by acpi_parse_madt) */
const void *acpi_find_rsdp(void);

#endif
