#ifndef SCREEN_H
#define SCREEN_H

void init_screen(void);
void print_char(char c);
void print_string(const char *str);
void print_hex(unsigned int num);
void print_int(unsigned int num);
void clear_screen(void);
void set_cursor(int x, int y);
int get_cursor_x(void);
int get_cursor_y(void);
void set_capture(int on);
const char *get_capture(void);

/* Ring-3-safe wrappers (no cli) */
void clear_screen_user(void);
void set_cursor_user(int x, int y);
void print_char_user(char c);
void print_string_user(const char *str);
void print_hex_user(unsigned int num);
void print_int_user(unsigned int num);

#endif
