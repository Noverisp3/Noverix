#include "serial.h"
#include "../cpu/ports.h"

#define COM1 0x3F8

static int is_transmit_empty(void)
{
    return inb(COM1 + 5) & 0x20;
}

void init_serial(void)
{
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x01);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_write_char(char c)
{
    while (!is_transmit_empty());
    outb(COM1, c);
}

void serial_write_string(const char *str)
{
    while (*str)
        serial_write_char(*str++);
}

void serial_write_hex(unsigned int num)
{
    char hex[9];
    int i;
    hex[0] = '0';
    hex[1] = 'x';
    for (i = 7; i >= 2; i--) {
        unsigned char nibble = (unsigned char)(num & 0x0F);
        hex[i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        num >>= 4;
    }
    hex[8] = '\0';
    serial_write_string(hex);
}
