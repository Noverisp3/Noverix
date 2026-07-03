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

#endif
