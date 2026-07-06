#include "pfa.h"
#include "../drivers/serial.h"
#include "../sync/sync.h"

extern unsigned int bss_end;

static unsigned char bitmap[MAX_FRAMES / 8];
static unsigned int total_frames;
static spinlock_t pfa_lock = SPINLOCK_INIT;

#define FRAME(addr) ((unsigned int)(addr) / FRAME_SIZE)
#define ADDR(frame) ((void *)((frame) * FRAME_SIZE))

static void set_bit(unsigned int frame)
{
    bitmap[frame / 8] |= (1 << (frame % 8));
}

static void clear_bit(unsigned int frame)
{
    bitmap[frame / 8] &= ~(1 << (frame % 8));
}

static int test_bit(unsigned int frame)
{
    return (bitmap[frame / 8] >> (frame % 8)) & 1;
}

static void mark_frame(unsigned int addr)
{
    unsigned int frame = FRAME(addr);
    if (frame < total_frames) set_bit(frame);
}

void pfa_init(unsigned int detected_ram)
{
    unsigned int bss_end_addr = (unsigned int)&bss_end;

    if (detected_ram > MAX_MEMORY)
        detected_ram = MAX_MEMORY;
    total_frames = detected_ram / FRAME_SIZE;

    serial_write_string("[pfa] init frames=");
    serial_write_hex(total_frames);
    serial_write_string(" ram=");
    serial_write_hex(detected_ram);
    serial_write_string(" bss_end=");
    serial_write_hex(bss_end_addr);
    serial_write_char('\n');

    unsigned int kernel_end = (bss_end_addr + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    for (unsigned int a = 0x00000000; a < kernel_end; a += FRAME_SIZE)
        mark_frame(a);

    mark_frame(0x00090000);
    mark_frame(0x000A0000);
    for (unsigned int a = 0x000A0000; a < 0x00100000; a += FRAME_SIZE)
        mark_frame(a);
}

void *alloc_frame(void)
{
    unsigned int flags = spinlock_lock_irqsave(&pfa_lock);
    for (unsigned int i = 0; i < total_frames; i++) {
        if (!test_bit(i)) {
            set_bit(i);
            spinlock_unlock_irqrestore(&pfa_lock, flags);
            return ADDR(i);
        }
    }
    spinlock_unlock_irqrestore(&pfa_lock, flags);
    serial_write_string("[pfa] OOM\n");
    return 0;
}

void free_frame(void *addr)
{
    if (!addr) return;
    unsigned int flags = spinlock_lock_irqsave(&pfa_lock);
    unsigned int frame = FRAME(addr);
    if (frame < total_frames) clear_bit(frame);
    spinlock_unlock_irqrestore(&pfa_lock, flags);
}

int get_free_frame_count(void)
{
    unsigned int flags = spinlock_lock_irqsave(&pfa_lock);
    int count = 0;
    for (unsigned int i = 0; i < total_frames; i++)
        if (!test_bit(i)) count++;
    spinlock_unlock_irqrestore(&pfa_lock, flags);
    return count;
}

void *alloc_frames(unsigned int count)
{
    if (count == 0 || count > total_frames) return 0;
    unsigned int flags = spinlock_lock_irqsave(&pfa_lock);
    for (unsigned int start = 0; start <= total_frames - count; start++)
    {
        int ok = 1;
        for (unsigned int j = 0; j < count; j++)
        {
            if (test_bit(start + j)) { ok = 0; break; }
        }
        if (ok)
        {
            for (unsigned int j = 0; j < count; j++)
                set_bit(start + j);
            spinlock_unlock_irqrestore(&pfa_lock, flags);
            return ADDR(start);
        }
    }
    spinlock_unlock_irqrestore(&pfa_lock, flags);
    serial_write_string("[pfa] OOM for contiguous frames\n");
    return 0;
}

void free_frames(void *addr, unsigned int count)
{
    unsigned int flags = spinlock_lock_irqsave(&pfa_lock);
    unsigned int start = FRAME(addr);
    for (unsigned int j = 0; j < count; j++)
        if (start + j < total_frames)
            clear_bit(start + j);
    spinlock_unlock_irqrestore(&pfa_lock, flags);
}
