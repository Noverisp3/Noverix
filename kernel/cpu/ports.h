#ifndef PORTS_H
#define PORTS_H

static inline unsigned char inb(unsigned short port)
{
    unsigned char result;
    __asm__ volatile ("inb %1, %0" : "=a" (result) : "Nd" (port));
    return result;
}

static inline void outb(unsigned short port, unsigned char data)
{
    __asm__ volatile ("outb %0, %1" : : "a" (data), "Nd" (port));
}

static inline unsigned short inw(unsigned short port)
{
    unsigned short result;
    __asm__ volatile ("inw %1, %0" : "=a" (result) : "Nd" (port));
    return result;
}

static inline void outw(unsigned short port, unsigned short data)
{
    __asm__ volatile ("outw %0, %1" : : "a" (data), "Nd" (port));
}

static inline unsigned int inl(unsigned short port)
{
    unsigned int result;
    __asm__ volatile ("inl %1, %0" : "=a" (result) : "Nd" (port));
    return result;
}

static inline void outl(unsigned short port, unsigned int data)
{
    __asm__ volatile ("outl %0, %1" : : "a" (data), "Nd" (port));
}

#endif
