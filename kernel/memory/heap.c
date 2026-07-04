#include "heap.h"
#include "../drivers/serial.h"
#include "../lib.h"
#include "../sync/sync.h"

#define HEADER_SIZE 4
#define FOOTER_SIZE 4
#define MIN_BLOCK (HEADER_SIZE + FOOTER_SIZE + 4)
#define ALIGN 4

static spinlock_t heap_lock = SPINLOCK_INIT;

static void *malloc_impl(unsigned int size);
static void *realloc_impl(void *ptr, unsigned int size);
static void *calloc_impl(unsigned int num, unsigned int size);
static void free_impl(void *ptr);
static void heap_walk_impl(void);

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

static void *malloc_impl(unsigned int size)
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

void *realloc_impl(void *ptr, unsigned int size)
{
    if (!ptr)
        return malloc_impl(size);

    if (size == 0)
    {
        free_impl(ptr);
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

    void *new_ptr = malloc_impl(size);
    if (!new_ptr)
        return 0;

    unsigned int copy_size = old_data_size < size ? old_data_size : size;
    unsigned char *d = (unsigned char *)new_ptr;
    unsigned char *s = (unsigned char *)ptr;
    for (unsigned int i = 0; i < copy_size; i++)
        d[i] = s[i];

    free_impl(ptr);
    return new_ptr;
}

void free_impl(void *ptr)
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

void *calloc_impl(unsigned int num, unsigned int size)
{
    unsigned int total = num * size;
    void *ptr = malloc_impl(total);
    if (ptr)
        lib_memset(ptr, 0, total);
    return ptr;
}

void heap_walk_impl(void)
{
    unsigned int addr = HEAP_START;
    unsigned int total_free = 0, total_used = 0, blocks = 0;
    serial_write_string("[heap] walk:\n");
    while (addr < HEAP_START + HEAP_SIZE)
    {
        unsigned int *hdr = (unsigned int *)addr;
        unsigned int block_size = *hdr & ~1;
        int is_free = !(*hdr & 1);
        if (block_size == 0 || block_size > HEAP_SIZE)
        {
            serial_write_string("  CORRUPT at ");
            serial_write_hex(addr);
            serial_write_char('\n');
            break;
        }
        serial_write_string("  ");
        serial_write_hex(addr);
        serial_write_string(is_free ? " free " : " used ");
        serial_write_hex(block_size);
        serial_write_string(" (data=");
        serial_write_int(block_size - HEADER_SIZE - FOOTER_SIZE);
        serial_write_string(")\n");
        if (is_free) total_free += block_size;
        else total_used += block_size;
        blocks++;
        addr += block_size;
    }
    serial_write_string("  blocks=");
    serial_write_int(blocks);
    serial_write_string(" free=");
    serial_write_hex(total_free);
    serial_write_string(" used=");
    serial_write_hex(total_used);
    serial_write_char('\n');
}

/* ── Public SMP-safe wrappers ── */

void *malloc(unsigned int size)
{
    unsigned int f = spinlock_lock_irqsave(&heap_lock);
    void *p = malloc_impl(size);
    spinlock_unlock_irqrestore(&heap_lock, f);
    return p;
}

void *realloc(void *ptr, unsigned int size)
{
    unsigned int f = spinlock_lock_irqsave(&heap_lock);
    void *p = realloc_impl(ptr, size);
    spinlock_unlock_irqrestore(&heap_lock, f);
    return p;
}

void *calloc(unsigned int num, unsigned int size)
{
    unsigned int f = spinlock_lock_irqsave(&heap_lock);
    void *p = calloc_impl(num, size);
    spinlock_unlock_irqrestore(&heap_lock, f);
    return p;
}

void free(void *ptr)
{
    unsigned int f = spinlock_lock_irqsave(&heap_lock);
    free_impl(ptr);
    spinlock_unlock_irqrestore(&heap_lock, f);
}

void heap_walk(void)
{
    unsigned int f = spinlock_lock_irqsave(&heap_lock);
    heap_walk_impl();
    spinlock_unlock_irqrestore(&heap_lock, f);
}