#include "graphics.h"
#include "font.h"

static unsigned char *lfb_ptr;
static int fb_active;
static unsigned short fb_width, fb_height, fb_pitch, fb_bpp, fb_bpp_bytes;

static inline void pixel_write(unsigned char *p, unsigned int color)
{
    p[0] = color & 0xFF;
    p[1] = (color >> 8) & 0xFF;
    p[2] = (color >> 16) & 0xFF;
    if (fb_bpp_bytes == 4) p[3] = (color >> 24) & 0xFF;
}

void init_graphics(unsigned int lfb, unsigned short width,
                   unsigned short height, unsigned short pitch,
                   unsigned short bpp)
{
    lfb_ptr = (unsigned char *)lfb;
    fb_width = width;
    fb_height = height;
    fb_pitch = pitch;
    fb_bpp = bpp;
    fb_bpp_bytes = bpp / 8;
    fb_active = 1;
}

int is_graphics_active(void)
{
    return fb_active;
}

void draw_pixel(int x, int y, unsigned int color)
{
    if (x < 0 || (unsigned)x >= fb_width || y < 0 || (unsigned)y >= fb_height)
        return;
    pixel_write(lfb_ptr + y * fb_pitch + x * fb_bpp_bytes, color);
}

void fill_rect(int x, int y, int w, int h, unsigned int color)
{
    for (int row = y; row < y + h && row < (int)fb_height; row++) {
        unsigned char *p = lfb_ptr + row * fb_pitch + x * fb_bpp_bytes;
        int max_col = x + w;
        if (max_col > (int)fb_width) max_col = fb_width;
        for (int col = x; col < max_col; col++) {
            pixel_write(p, color);
            p += fb_bpp_bytes;
        }
    }
}

static unsigned char char_row(unsigned char c, int row)
{
    if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) c = '?';
    return font_data[c - FONT_FIRST_CHAR][row];
}

void draw_char_gfx(int x, int y, unsigned char c, unsigned int fg, unsigned int bg)
{
    for (int row = 0; row < FONT_HEIGHT; row++) {
        unsigned char bits = char_row(c, row);
        unsigned char *p = lfb_ptr + (y + row) * fb_pitch + x * fb_bpp_bytes;
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (bits & (0x80 >> col))
                pixel_write(p, fg);
            else if (bg != 0xFFFFFFFF)
                pixel_write(p, bg);
            p += fb_bpp_bytes;
        }
    }
}

int fb_cols(void) { return fb_width; }
int fb_rows(void) { return fb_height; }

void scroll_gfx(int lines)
{
    if (lines <= 0 || (unsigned)lines * FONT_HEIGHT >= fb_height) {
        fill_rect(0, 0, fb_width, fb_height, 0x00000000);
        return;
    }
    int pixel_lines = lines * FONT_HEIGHT;
    unsigned char *src = lfb_ptr + pixel_lines * fb_pitch;
    int copy_bytes = (fb_height - pixel_lines) * fb_pitch;
    for (int i = 0; i < copy_bytes; i++)
        lfb_ptr[i] = src[i];
    fill_rect(0, fb_height - pixel_lines, fb_width, pixel_lines, 0x00000000);
}
