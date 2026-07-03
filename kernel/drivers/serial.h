#ifndef SERIAL_H
#define SERIAL_H

void init_serial(void);
void serial_write_char(char c);
void serial_write_string(const char *str);
void serial_write_hex(unsigned int num);
int serial_data_available(void);
char serial_read_char(void);

#endif
