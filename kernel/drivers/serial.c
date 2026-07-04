#include "serial.h"
#include "../cpu/ports.h"
#include "../sync/sync.h"

#define COM1 0x3F8

static spinlock_t serial_lock = SPINLOCK_INIT;

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

static void serial_write_char_impl(char c)
{
    while (!is_transmit_empty());
    if (c == '\n')
        outb(COM1, '\r');
    outb(COM1, c);
}

void serial_write_char(char c)
{
    unsigned int flags = spinlock_lock_irqsave(&serial_lock);
    serial_write_char_impl(c);
    spinlock_unlock_irqrestore(&serial_lock, flags);
}

void serial_write_string(const char *str)
{
    unsigned int flags = spinlock_lock_irqsave(&serial_lock);
    while (*str)
        serial_write_char_impl(*str++);
    spinlock_unlock_irqrestore(&serial_lock, flags);
}

void serial_write_hex(unsigned int num)
{
    char hex[11];
    int i;
    hex[0] = '0';
    hex[1] = 'x';
    for (i = 9; i >= 2; i--) {
        unsigned char nibble = (unsigned char)(num & 0x0F);
        hex[i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        num >>= 4;
    }
    hex[10] = '\0';
    unsigned int flags = spinlock_lock_irqsave(&serial_lock);
    for (char *p = hex; *p; p++)
        serial_write_char_impl(*p);
    spinlock_unlock_irqrestore(&serial_lock, flags);
}

void serial_write_int(unsigned int num)
{
    char buf[12];
    int i = 11;
    buf[11] = 0;
    unsigned int flags = spinlock_lock_irqsave(&serial_lock);
    if (num == 0)
    {
        serial_write_char_impl('0');
        spinlock_unlock_irqrestore(&serial_lock, flags);
        return;
    }
    while (num && i > 0)
    {
        i--;
        buf[i] = '0' + (num % 10);
        num /= 10;
    }
    for (int j = i; buf[j]; j++)
        serial_write_char_impl(buf[j]);
    spinlock_unlock_irqrestore(&serial_lock, flags);
}

int serial_data_available(void)
{
    return inb(COM1 + 5) & 1;
}

char serial_read_char(void)
{
    while (!serial_data_available());
    return inb(COM1);
}
