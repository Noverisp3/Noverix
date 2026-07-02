#include "screen.h"
#include "../cpu/ports.h"

#define VIDEO_MEMORY 0xB8000
#define MAX_ROWS 25
#define MAX_COLS 80
#define WHITE_ON_BLACK 0x0F

static int cursor_x;
static int cursor_y;

void clear_screen(void)
{
    unsigned short *video = (unsigned short *)VIDEO_MEMORY;
    int i;
    for (i = 0; i < MAX_ROWS * MAX_COLS; i++)
        video[i] = (WHITE_ON_BLACK << 8) | ' ';
    cursor_x = 0;
    cursor_y = 0;
    set_cursor(0, 0);
}

void init_screen(void)
{
    clear_screen();
}

void set_cursor(int x, int y)
{
    unsigned short pos = y * MAX_COLS + x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

static void scroll(void)
{
    unsigned short *video = (unsigned short *)VIDEO_MEMORY;
    int x, y;
    for (y = 0; y < MAX_ROWS - 1; y++)
        for (x = 0; x < MAX_COLS; x++)
            video[y * MAX_COLS + x] = video[(y + 1) * MAX_COLS + x];
    for (x = 0; x < MAX_COLS; x++)
        video[(MAX_ROWS - 1) * MAX_COLS + x] = (WHITE_ON_BLACK << 8) | ' ';
    cursor_y = MAX_ROWS - 1;
}

void print_char(char c)
{
    unsigned short *video = (unsigned short *)VIDEO_MEMORY;
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) cursor_x--;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
    } else {
        video[cursor_y * MAX_COLS + cursor_x] = (WHITE_ON_BLACK << 8) | (unsigned char)c;
        cursor_x++;
    }
    if (cursor_x >= MAX_COLS) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= MAX_ROWS)
        scroll();
    set_cursor(cursor_x, cursor_y);
}

void print_string(const char *str)
{
    while (*str)
        print_char(*str++);
}

void print_hex(unsigned int num)
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
    print_string(hex);
}
