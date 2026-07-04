#ifndef KEYBOARD_H
#define KEYBOARD_H

#define KEY_UP 1
#define KEY_DOWN 2
#define KEY_LEFT 3
#define KEY_RIGHT 4

void init_keyboard(void);
char get_char(void);
char read_char(void);
void keyboard_set_typematic(unsigned char param);
unsigned char keyboard_get_typematic(void);

/* Ring-3-safe wrappers (no cli) */
char get_char_user(void);
char read_char_user(void);

#endif
