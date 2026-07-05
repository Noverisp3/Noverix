#include "noverix.h"

#define SEG_UDATA 0x23

void main(void) {
    __asm__ volatile (
        "mov %0, %%ds\n"
        "mov %0, %%es\n"
        "mov %0, %%fs\n"
        "mov %0, %%gs\n"
        : : "r"(SEG_UDATA)
    );

    if (!is_graphics_active()) {
        print_string("App requires graphics mode\n");
        sys_exit();
    }

    int w = fb_cols();
    int h = fb_rows();
    unsigned int color = 0x00FF0000;
    unsigned int delay = 10;

    clear_screen();

    int x1 = 0, y1 = h - 1;
    int x2 = w / 2, y2 = 0;
    int x3 = w - 1, y3 = h - 1;
    int x = 0, y = 0;

    int i;
    for (i = 0; i < 50000; i++) {
        int r = (int)(get_ticks() % 3);
        if (r == 0) {
            x = (x + x1) / 2;
            y = (y + y1) / 2;
        } else if (r == 1) {
            x = (x + x2) / 2;
            y = (y + y2) / 2;
        } else {
            x = (x + x3) / 2;
            y = (y + y3) / 2;
        }
        draw_pixel(x, y, color);
        sleep_ms(delay);
    }

    print_string("Sierpinski triangle done. Press any key...\n");
    read_char();
    sys_exit();
}
