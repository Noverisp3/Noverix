#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>

#define RTL_VENDOR  0x10EC
#define RTL_DEVICE  0x8139

int  rtl8139_init(void);
void rtl8139_get_mac(uint8_t mac[6]);
int  rtl8139_send(const void *data, uint16_t len);
int  rtl8139_recv(uint8_t *buf, uint16_t *len);

#endif
