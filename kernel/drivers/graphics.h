#ifndef GRAPHICS_H
#define GRAPHICS_H

void init_graphics(unsigned int lfb, unsigned short width,
                   unsigned short height, unsigned short pitch,
                   unsigned short bpp);
int is_graphics_active(void);
void draw_pixel(int x, int y, unsigned int color);
void fill_rect(int x, int y, int w, int h, unsigned int color);
void draw_char_gfx(int x, int y, unsigned char c, unsigned int fg, unsigned int bg);
void scroll_gfx(int lines);
int fb_cols(void);
int fb_rows(void);

#endif
