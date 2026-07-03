// FILE: triangle.c

#include "noverix.h"

void draw_line(noverix_api_t *api, int x0, int y0, int x1, int y1, unsigned int color);
void draw_sierpinski(noverix_api_t *api, int x1, int y1, int x2, int y2, int x3, int y3, int depth, unsigned int color);

void main(noverix_api_t *api) {
    if (!api->is_graphics_active()) {
        api->print_string("Please start Noverix in Graphics Mode to run this app!\n");
        return;
    }

    api->clear_screen();

    int width = api->fb_cols() * 8;
    int height = api->fb_rows() * 16;

    int margin = 40;
    int x1 = width / 2;
    int y1 = margin;
    int x2 = margin;
    int y2 = height - margin;
    int x3 = width - margin;
    int y3 = height - margin;

    draw_sierpinski(api, x1, y1, x2, y2, x3, y3, 5, 0x00FFFF);

    api->read_char();
}

void draw_line(noverix_api_t *api, int x0, int y0, int x1, int y1, unsigned int color) {
    int dx = x1 - x0; if (dx < 0) dx = -dx;
    int dy = y1 - y0; if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (1) {
        api->draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void draw_sierpinski(noverix_api_t *api, int x1, int y1, int x2, int y2, int x3, int y3, int depth, unsigned int color) {
    if (depth == 0) {
        draw_line(api, x1, y1, x2, y2, color);
        draw_line(api, x2, y2, x3, y3, color);
        draw_line(api, x3, y3, x1, y1, color);
        return;
    }

    int mx12 = (x1 + x2) / 2;
    int my12 = (y1 + y2) / 2;
    int mx23 = (x2 + x3) / 2;
    int my23 = (y2 + y3) / 2;
    int mx31 = (x3 + x1) / 2;
    int my31 = (y3 + y1) / 2;

    draw_sierpinski(api, x1, y1, mx12, my12, mx31, my31, depth - 1, color);
    draw_sierpinski(api, mx12, my12, x2, y2, mx23, my23, depth - 1, color);
    draw_sierpinski(api, mx31, my31, mx23, my23, x3, y3, depth - 1, color);
}