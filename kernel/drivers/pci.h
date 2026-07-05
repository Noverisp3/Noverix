#ifndef PCI_H
#define PCI_H

#include <stdint.h>

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
uint16_t pci_find_device(uint16_t vendor, uint16_t device);
uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t func, int bar);
uint8_t pci_read_int_line(uint8_t bus, uint8_t slot, uint8_t func);

/* Packed: (bus << 8) | (slot << 3) | func */
void pci_unpack(uint16_t dev, uint8_t *bus, uint8_t *slot, uint8_t *func);

#endif
