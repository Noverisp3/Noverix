#include "pci.h"
#include "../cpu/ports.h"

#define PCI_ADDR_PORT  0xCF8
#define PCI_DATA_PORT  0xCFC

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) |
                    ((uint32_t)(slot & 0x1F) << 11) |
                    ((uint32_t)(func & 0x07) << 8) |
                    (offset & 0xFC);
    outl(PCI_ADDR_PORT, addr);
    return inl(PCI_DATA_PORT);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val)
{
    uint32_t addr = 0x80000000 | ((uint32_t)bus << 16) |
                    ((uint32_t)(slot & 0x1F) << 11) |
                    ((uint32_t)(func & 0x07) << 8) |
                    (offset & 0xFC);
    outl(PCI_ADDR_PORT, addr);
    outl(PCI_DATA_PORT, val);
}

uint16_t pci_find_device(uint16_t vendor, uint16_t device)
{
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t vd = pci_config_read(bus, slot, func, 0);
                if ((vd & 0xFFFF) == vendor && ((vd >> 16) & 0xFFFF) == device)
                    return (uint16_t)((bus << 8) | (slot << 3) | func);
                if (func == 0) {
                    uint32_t hdr = pci_config_read(bus, slot, func, 0x0C);
                    if (!(hdr & 0x800000))
                        break;
                }
            }
        }
    }
    return 0xFFFF;
}

uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar)
{
    return pci_config_read(bus, slot, func, 0x10 + bar * 4);
}

uint8_t pci_read_int_line(uint8_t bus, uint8_t slot, uint8_t func)
{
    return (uint8_t)(pci_config_read(bus, slot, func, 0x3C) >> 8);
}

void pci_unpack(uint16_t dev, uint8_t *bus, uint8_t *slot, uint8_t *func)
{
    if (bus)   *bus   = (dev >> 8) & 0xFF;
    if (slot)  *slot  = (dev >> 3) & 0x1F;
    if (func)  *func  = dev & 0x07;
}
