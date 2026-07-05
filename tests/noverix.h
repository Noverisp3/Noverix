#ifndef NOVERIX_H
#define NOVERIX_H

#define SYS_EXIT             0
#define SYS_CLEAR_SCREEN     1
#define SYS_PRINT_CHAR       2
#define SYS_PRINT_STRING     3
#define SYS_PRINT_HEX        4
#define SYS_PRINT_INT        5
#define SYS_SET_CURSOR       6
#define SYS_GET_CHAR         7
#define SYS_READ_CHAR        8
#define SYS_IS_GRAPHICS      9
#define SYS_DRAW_PIXEL       10
#define SYS_FILL_RECT        11
#define SYS_FB_COLS          12
#define SYS_FB_ROWS          13
#define SYS_SLEEP_MS         14
#define SYS_GET_TICKS        15
#define SYS_MALLOC           16
#define SYS_FREE             17
#define SYS_NVFS_READ        18
#define SYS_NVFS_WRITE       19
#define SYS_NVFS_APPEND      20
#define SYS_NVFS_DELETE      21
#define SYS_NVFS_MKDIR       22
#define SYS_NVFS_RMDIR       23
#define SYS_NVFS_CHDIR       24
#define SYS_NVFS_GET_CWD     25
#define SYS_NVFS_LIST        26
#define SYS_NVFS_STRERROR    27
#define SYS_NVFS_IS_MOUNTED  28
#define SYS_YIELD            29

static inline void sys_exit(void) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_EXIT) : "memory");
}

static inline void clear_screen(void) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_CLEAR_SCREEN) : "memory");
}

static inline void print_char(char c) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_PRINT_CHAR), "b"((unsigned int)(unsigned char)c) : "memory");
}

static inline void print_string(const char *str) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_PRINT_STRING), "b"(str) : "memory");
}

static inline void print_hex(unsigned int num) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_PRINT_HEX), "b"(num) : "memory");
}

static inline void print_int(unsigned int num) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_PRINT_INT), "b"(num) : "memory");
}

static inline void set_cursor(int x, int y) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_SET_CURSOR), "b"(x), "c"(y) : "memory");
}

static inline char get_char(void) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_GET_CHAR) : "memory");
    return (char)ret;
}

static inline char read_char(void) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_READ_CHAR) : "memory");
    return (char)ret;
}

static inline int is_graphics_active(void) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_IS_GRAPHICS) : "memory");
    return ret;
}

static inline void draw_pixel(int x, int y, unsigned int color) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_DRAW_PIXEL), "b"(x), "c"(y), "d"(color) : "memory");
}

static inline void fill_rect(int x, int y, int w, int h, unsigned int color) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_FILL_RECT), "b"(x), "c"(y), "d"(w), "S"(h), "D"(color) : "memory");
}

static inline int fb_cols(void) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_FB_COLS) : "memory");
    return ret;
}

static inline int fb_rows(void) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_FB_ROWS) : "memory");
    return ret;
}

static inline void sleep_ms(unsigned int ms) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_SLEEP_MS), "b"(ms) : "memory");
}

static inline void sys_yield(void) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_YIELD) : "memory");
}

static inline unsigned int get_ticks(void) {
    unsigned int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_GET_TICKS) : "memory");
    return ret;
}

static inline void *malloc(unsigned int size) {
    void *ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_MALLOC), "b"(size) : "memory");
    return ret;
}

static inline void free(void *ptr) {
    __asm__ volatile ("int $0x80" : : "a"(SYS_FREE), "b"(ptr) : "memory");
}

static inline int nvfs_read(const char *path, void *out, unsigned max) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_READ), "b"(path), "c"(out), "d"(max) : "memory");
    return ret;
}

static inline int nvfs_write(const char *path, const void *data, unsigned size) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_WRITE), "b"(path), "c"(data), "d"(size) : "memory");
    return ret;
}

static inline int nvfs_append(const char *path, const void *data, unsigned size) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_APPEND), "b"(path), "c"(data), "d"(size) : "memory");
    return ret;
}

static inline int nvfs_delete(const char *path) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_DELETE), "b"(path) : "memory");
    return ret;
}

static inline int nvfs_mkdir(const char *path) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_MKDIR), "b"(path) : "memory");
    return ret;
}

static inline int nvfs_rmdir(const char *path) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_RMDIR), "b"(path) : "memory");
    return ret;
}

static inline int nvfs_chdir(const char *path, unsigned *out_inode) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_CHDIR), "b"(path), "c"(out_inode) : "memory");
    return ret;
}

static inline unsigned nvfs_get_cwd(void) {
    unsigned ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_GET_CWD) : "memory");
    return ret;
}

static inline int nvfs_list(const char *path) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_LIST), "b"(path) : "memory");
    return ret;
}

static inline const char *nvfs_strerror(int err) {
    const char *ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_STRERROR), "b"(err) : "memory");
    return ret;
}

static inline int nvfs_is_mounted(void) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "0"(SYS_NVFS_IS_MOUNTED) : "memory");
    return ret;
}

#endif
