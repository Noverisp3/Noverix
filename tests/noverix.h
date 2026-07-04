// FILE: noverix.h

#ifndef NOVERIX_H
#define NOVERIX_H

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

    void* (*malloc)(unsigned int size);
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
    const char* (*nvfs_strerror)(int err);
    int (*nvfs_is_mounted)(void);
    void (*exit)(void);
} noverix_api_t;

#endif