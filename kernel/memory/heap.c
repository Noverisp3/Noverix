#include "heap.h"
#include "../drivers/serial.h"

#define HEADER_SIZE 4
#define FOOTER_SIZE 4
#define MIN_BLOCK (HEADER_SIZE + FOOTER_SIZE + 4)
#define ALIGN 4

static void set_footer(unsigned int addr, unsigned int size)
{
    *(unsigned int *)(addr + size - FOOTER_SIZE) = size;
}

void heap_init(void)
{
    unsigned int *hdr = (unsigned int *)HEAP_START;
    *hdr = HEAP_SIZE;
    set_footer(HEAP_START, HEAP_SIZE);
    serial_write_string("[heap] init base=");
    serial_write_hex(HEAP_START);
    serial_write_string(" size=");
    serial_write_hex(HEAP_SIZE);
    serial_write_string(" end=");
    serial_write_hex(HEAP_START + HEAP_SIZE);
    serial_write_char('\n');
}

void *malloc(unsigned int size)
{
    if (size == 0)
        return 0;

    if (size > HEAP_SIZE)
    {
        serial_write_string("[heap] malloc: requested size too large\n");
        return 0;
    }

    size = (size + ALIGN - 1) & ~(ALIGN - 1);
    unsigned int need = HEADER_SIZE + size + FOOTER_SIZE;
    if (need < MIN_BLOCK)
        need = MIN_BLOCK;

    unsigned int addr = HEAP_START;
    while (addr < HEAP_START + HEAP_SIZE)
    {
        unsigned int *hdr = (unsigned int *)addr;
        unsigned int block_size = *hdr & ~1;
        int is_free = !(*hdr & 1);

        if (block_size == 0 || block_size > HEAP_SIZE)
        {
            serial_write_string("[heap] malloc: CORRUPTED block size found! Resetting heap...\n");
            heap_init();
            return 0;
        }

        if (is_free && block_size >= need)
        {
            unsigned int remaining = block_size - need;
            if (remaining >= MIN_BLOCK)
            {
                *hdr = need | 1;
                set_footer(addr, need);
                unsigned int *next_hdr = (unsigned int *)(addr + need);
                *next_hdr = remaining;
                set_footer(addr + need, remaining);
            }
            else
            {
                need = block_size;
                *hdr = need | 1;
                set_footer(addr, need);
            }
            return (void *)(addr + HEADER_SIZE);
        }
        addr += block_size;
    }
    serial_write_string("[heap] OOM\n");
    return 0;
}

void *realloc(void *ptr, unsigned int size)
{
    if (!ptr)
        return malloc(size);

    if (size == 0)
    {
        free(ptr);
        return 0;
    }

    unsigned int addr = (unsigned int)ptr - HEADER_SIZE;
    if (addr < HEAP_START || addr >= HEAP_START + HEAP_SIZE)
    {
        serial_write_string("[heap] realloc: invalid pointer\n");
        return 0;
    }

    unsigned int *hdr = (unsigned int *)addr;
    unsigned int old_size = *hdr & ~1;
    if (!(*hdr & 1))
    {
        serial_write_string("[heap] realloc: pointer not allocated\n");
        return 0;
    }

    unsigned int old_data_size = old_size - HEADER_SIZE - FOOTER_SIZE;

    size = (size + ALIGN - 1) & ~(ALIGN - 1);
    unsigned int new_need = HEADER_SIZE + size + FOOTER_SIZE;
    if (new_need < MIN_BLOCK)
        new_need = MIN_BLOCK;

    if (new_need <= old_size)
    {
        unsigned int remaining = old_size - new_need;
        if (remaining >= MIN_BLOCK)
        {
            *hdr = new_need | 1;
            set_footer(addr, new_need);
            unsigned int *next_hdr = (unsigned int *)(addr + new_need);
            *next_hdr = remaining;
            set_footer(addr + new_need, remaining);
        }
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr)
        return 0;

    unsigned int copy_size = old_data_size < size ? old_data_size : size;
    unsigned char *d = (unsigned char *)new_ptr;
    unsigned char *s = (unsigned char *)ptr;
    for (unsigned int i = 0; i < copy_size; i++)
        d[i] = s[i];

    free(ptr);
    return new_ptr;
}

void free(void *ptr)
{
    if (!ptr)
        return;
    unsigned int addr = (unsigned int)ptr - HEADER_SIZE;

    if (addr < HEAP_START || addr >= HEAP_START + HEAP_SIZE)
    {
        serial_write_string("[heap] free: invalid pointer outside heap boundaries\n");
        return;
    }

    unsigned int *hdr = (unsigned int *)addr;
    unsigned int size = *hdr & ~1;

    if (!(*hdr & 1))
    {
        serial_write_string("[heap] double free at ");
        serial_write_hex(addr);
        serial_write_string("\n");
        return;
    }

    *hdr = size;
    set_footer(addr, size);

    unsigned int next_addr = addr + size;
    if (next_addr >= HEAP_START && next_addr < HEAP_START + HEAP_SIZE)
    {
        unsigned int *next_hdr = (unsigned int *)next_addr;
        unsigned int next_size = *next_hdr & ~1;

        if (next_size >= MIN_BLOCK && next_size < HEAP_SIZE && !(*next_hdr & 1))
        {
            size += next_size;
            *hdr = size;
            set_footer(addr, size);
        }
    }

    if (addr > HEAP_START + MIN_BLOCK)
    {
        unsigned int prev_size = *(unsigned int *)(addr - FOOTER_SIZE);

        if (prev_size >= MIN_BLOCK && prev_size < HEAP_SIZE)
        {
            unsigned int prev_addr = addr - prev_size;

            if (prev_addr >= HEAP_START && prev_addr < addr)
            {
                unsigned int *prev_hdr = (unsigned int *)prev_addr;
                if (!(*prev_hdr & 1))
                {
                    size += prev_size;
                    *prev_hdr = size;
                    set_footer(prev_addr, size);
                }
            }
        }
    }
}