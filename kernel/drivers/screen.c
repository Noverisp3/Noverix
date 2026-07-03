#include "screen.h"
#include "serial.h"
#include "graphics.h"
#include "font.h"
#include "../cpu/ports.h"

#define GFX_FG 0x00C0C0C0
#define GFX_BG 0x00000000

#define VIDEO_MEMORY 0xB8000
#define MAX_ROWS 25
#define MAX_COLS 80
#define WHITE_ON_BLACK 0x0F

static int cursor_x;
static int cursor_y;

#define CAPTURE_SIZE 4096
static int capture_mode;
static char capture_buf[CAPTURE_SIZE];
static int capture_pos;

void set_capture(int on)
{
    capture_mode = on;
    if (on) capture_pos = 0;
    if (capture_pos >= CAPTURE_SIZE) capture_pos = CAPTURE_SIZE - 1;
    capture_buf[capture_pos] = 0;
}

const char *get_capture(void)
{
    if (capture_pos >= CAPTURE_SIZE) capture_pos = CAPTURE_SIZE - 1;
    capture_buf[capture_pos] = 0;
    return capture_buf;
}

void clear_screen(void)
{
    if (is_graphics_active()) {
        fill_rect(0, 0, fb_cols() * FONT_WIDTH, fb_rows() * FONT_HEIGHT, GFX_BG);
        cursor_x = 0;
        cursor_y = 0;
        return;
    }
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
    cursor_x = x;
    cursor_y = y;
    if (!is_graphics_active()) {
        unsigned short pos = y * MAX_COLS + x;
        outb(0x3D4, 0x0F);
        outb(0x3D5, (unsigned char)(pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
    }
}

static void scroll(void)
{
    unsigned short *video = (unsigned short *)VIDEO_MEMORY;
    int i;
    for (i = 0; i < (MAX_ROWS - 1) * MAX_COLS; i++)
        video[i] = video[i + MAX_COLS];
    for (i = (MAX_ROWS - 1) * MAX_COLS; i < MAX_ROWS * MAX_COLS; i++)
        video[i] = (WHITE_ON_BLACK << 8) | ' ';
    cursor_y = MAX_ROWS - 1;
}

void print_char(char c)
{
    if (capture_mode) {
        if (c == '\n') {
            if (capture_pos < CAPTURE_SIZE - 1)
                capture_buf[capture_pos++] = '\n';
        } else if (c >= ' ') {
            if (capture_pos < CAPTURE_SIZE - 1)
                capture_buf[capture_pos++] = c;
        }
        return;
    }
    if (is_graphics_active()) {
        if (c == '\n') {
            cursor_x = 0; cursor_y++;
        } else if (c == '\b') {
            if (cursor_x > 0) cursor_x--;
        } else if (c == '\r') {
            cursor_x = 0;
            fill_rect(0, cursor_y * FONT_HEIGHT, fb_cols() * FONT_WIDTH, FONT_HEIGHT, GFX_BG);
        } else if (c == '\t') {
            cursor_x = (cursor_x + 8) & ~7;
        } else if (c >= ' ') {
            draw_char_gfx(cursor_x * FONT_WIDTH, cursor_y * FONT_HEIGHT, (unsigned char)c, GFX_FG, GFX_BG);
            cursor_x++;
        }
        int max_cols = fb_cols();
        int max_rows = fb_rows();
        if (cursor_x >= max_cols) { cursor_x = 0; cursor_y++; }
        if (cursor_y >= max_rows) { scroll_gfx(1); cursor_y = max_rows - 1; }
        return;
    }
    unsigned short *video = (unsigned short *)VIDEO_MEMORY;
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) cursor_x--;
    } else if (c == '\r') {
        cursor_x = 0;
        for (int i = 0; i < MAX_COLS; i++)
            video[cursor_y * MAX_COLS + i] = (WHITE_ON_BLACK << 8) | ' ';
        set_cursor(cursor_x, cursor_y);
        return;
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
    if (!capture_mode)
        serial_write_string(str);
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

void print_int(unsigned int num)
{
    char buf[12];
    int i = 11;
    buf[11] = 0;
    if (num == 0) { print_string("0"); return; }
    while (num && i > 0) {
        i--;
        buf[i] = '0' + (num % 10);
        num /= 10;
    }
    print_string(buf + i);
}

int get_cursor_x(void) { return cursor_x; }
int get_cursor_y(void) { return cursor_y; }
