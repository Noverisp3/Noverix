#include "keyboard.h"
#include "../cpu/ports.h"
#include "../cpu/idt.h"
#include "../sync/sync.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

static spinlock_t kb_lock = SPINLOCK_INIT;

#define BACKSPACE 0x0E
#define ENTER 0x1C
#define LSHIFT_DOWN 0x2A
#define LSHIFT_UP 0xAA
#define RSHIFT_DOWN 0x36
#define RSHIFT_UP 0xB6
#define CAPS_DOWN 0x3A
#define EXTENDED_PREFIX 0xE0

#define BUFFER_SIZE 256
#define TABLE_SIZE 58

static unsigned char scan_code;
static char shift_pressed;
static char caps_on;
static char extended;
static char key_buffer[BUFFER_SIZE];
static int buffer_head;
static int buffer_tail;
static unsigned char typematic_param;

static const char scancode_ascii[TABLE_SIZE] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=', '\b', 0, 'q', 'w', 'e', 'r',
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
    'm', ',', '.', '/', 0, '*', 0, ' '
};

static const char scancode_ascii_shift[TABLE_SIZE] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b', 0, 'Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    'M', '<', '>', '?', 0, '*', 0, ' '
};

static void keyboard_handler(registers_t *regs)
{
    (void)regs;
    unsigned char status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 0x01)) return;
    scan_code = inb(KEYBOARD_DATA_PORT);

    if (!spinlock_try_lock(&kb_lock))
        return;

    if (scan_code == EXTENDED_PREFIX) {
        extended = 1;
        spinlock_unlock(&kb_lock);
        return;
    }
    if (extended) {
        extended = 0;
        if (!(scan_code & 0x80)) {
            char c = 0;
            if (scan_code == 0x48) c = KEY_UP;
            else if (scan_code == 0x50) c = KEY_DOWN;
            else if (scan_code == 0x4B) c = KEY_LEFT;
            else if (scan_code == 0x4D) c = KEY_RIGHT;
            if (c) {
                int next = (buffer_head + 1) % BUFFER_SIZE;
                if (next != buffer_tail) {
                    key_buffer[buffer_head] = c;
                    buffer_head = next;
                }
            }
        }
        spinlock_unlock(&kb_lock);
        return;
    }
    if (scan_code == LSHIFT_DOWN || scan_code == RSHIFT_DOWN)
        shift_pressed = 1;
    else if (scan_code == LSHIFT_UP || scan_code == RSHIFT_UP)
        shift_pressed = 0;
    else if (scan_code == CAPS_DOWN) {
        caps_on = !caps_on;
    } else if (!(scan_code & 0x80) && scan_code < TABLE_SIZE) {
        char c;
        if (shift_pressed || caps_on)
            c = scancode_ascii_shift[scan_code];
        else
            c = scancode_ascii[scan_code];
        if (c) {
            int next = (buffer_head + 1) % BUFFER_SIZE;
            if (next != buffer_tail) {
                key_buffer[buffer_head] = c;
                buffer_head = next;
            }
        }
    }
    spinlock_unlock(&kb_lock);
}

char get_char(void)
{
    unsigned int flags = spinlock_lock_irqsave(&kb_lock);
    if (buffer_head == buffer_tail) {
        spinlock_unlock_irqrestore(&kb_lock, flags);
        return 0;
    }
    char c = key_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % BUFFER_SIZE;
    spinlock_unlock_irqrestore(&kb_lock, flags);
    return c;
}

char read_char(void)
{
    char c;
    while ((c = get_char()) == 0);
    return c;
}

void init_keyboard(void)
{
    shift_pressed = 0;
    caps_on = 0;
    extended = 0;
    buffer_head = 0;
    buffer_tail = 0;
    typematic_param = 0x00;
    register_interrupt_handler(33, keyboard_handler);
    keyboard_set_typematic(0x00); // 250ms delay, 30Hz repeat
}

void keyboard_set_typematic(unsigned char param)
{
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_DATA_PORT, 0xF3);
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_DATA_PORT, param);
    typematic_param = param;
}

unsigned char keyboard_get_typematic(void)
{
    return typematic_param;
}
