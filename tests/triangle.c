// FILE: triangle.c - Sierpinski triangle demo (ring 3 user app)

#define SEG_UDATA 0x23

typedef struct {
    void (*clear_screen)(void);
    void (*print_char)(char c);
    void (*print_string)(const char *str);
    void (*print_hex)(unsigned int num);
    void (*print_int)(unsigned int num);
    int (*get_cursor_x)(void);
    int (*get_cursor_y)(void);
    void (*set_cursor)(int x, int y);

    char (*get_char)(void);
    char (*read_char)(void);

    int (*is_graphics_active)(void);
    void (*draw_pixel)(int x, int y, unsigned int color);
    void (*fill_rect)(int x, int y, int w, int h, unsigned int color);
    int (*fb_cols)(void);
    int (*fb_rows)(void);

    void (*sleep_ms)(unsigned int ms);
    unsigned int (*get_ticks)(void);

    void *(*malloc)(unsigned int size);
    void (*free)(void *ptr);

    int (*nvfs_read)(const char *path, void *out, unsigned max);
    int (*nvfs_write)(const char *path, const void *data, unsigned size);
    int (*nvfs_append)(const char *path, const void *data, unsigned size);
    int (*nvfs_delete)(const char *path);
    int (*nvfs_mkdir)(const char *path);
    int (*nvfs_rmdir)(const char *path);
    int (*nvfs_chdir)(const char *path, unsigned *out_inode);
    unsigned (*nvfs_get_cwd)(void);
    int (*nvfs_list)(const char *path);
    const char *(*nvfs_strerror)(int err);
    int (*nvfs_is_mounted)(void);
    void (*exit)(void);
} noverix_api_t;

void main(void *arg) {
    noverix_api_t *api = (noverix_api_t *)arg;

    __asm__ volatile (
        "mov %0, %%ds\n"
        "mov %0, %%es\n"
        "mov %0, %%fs\n"
        "mov %0, %%gs\n"
        : : "r"(SEG_UDATA)
    );

    if (!api->is_graphics_active()) {
        api->print_string("App requires graphics mode\n");
        api->exit();
    }

    int w = api->fb_cols();
    int h = api->fb_rows();
    unsigned int color = 0x00FF0000;
    unsigned int delay = 10;

    api->clear_screen();

    int x1 = 0, y1 = h - 1;
    int x2 = w / 2, y2 = 0;
    int x3 = w - 1, y3 = h - 1;
    int x = 0, y = 0;

    int i;
    for (i = 0; i < 50000; i++) {
        int r = (int)((unsigned int)(api->get_ticks()) % 3);
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
        api->draw_pixel(x, y, color);
        api->sleep_ms(delay);
    }

    api->print_string("Sierpinski triangle done. Press any key...\n");
    api->read_char();
    api->exit();
}
