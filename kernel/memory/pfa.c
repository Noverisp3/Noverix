#include "pfa.h"
#include "../drivers/serial.h"

extern unsigned int bss_end;

static unsigned char bitmap[MAX_FRAMES / 8];
static unsigned int total_frames;

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

void pfa_init(void)
{
    unsigned int bss_end_addr = (unsigned int)&bss_end;
    total_frames = MAX_MEMORY / FRAME_SIZE;
    serial_write_string("[pfa] init frames=");
    serial_write_hex(total_frames);
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
    for (unsigned int i = 0; i < total_frames; i++) {
        if (!test_bit(i)) {
            set_bit(i);
            return ADDR(i);
        }
    }
    serial_write_string("[pfa] OOM\n");
    return 0;
}

void free_frame(void *addr)
{
    unsigned int frame = FRAME(addr);
    if (frame < total_frames) clear_bit(frame);
}
