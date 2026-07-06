#ifndef TCP_H
#define TCP_H

#include <stdint.h>

#define TCP_CLOSED      0
#define TCP_SYN_SENT    1
#define TCP_ESTABLISHED 2
#define TCP_FIN_WAIT_1  3
#define TCP_FIN_WAIT_2  4
#define TCP_TIME_WAIT   5
#define TCP_CLOSE_WAIT  6
#define TCP_LAST_ACK    7

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define TCP_MAX_SOCKS 4

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  offset_reserved;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_hdr_t;

typedef struct {
    int used;
    int state;
    uint32_t dst_ip;
    uint16_t dst_port;
    uint16_t src_port;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint32_t snd_una;
    uint16_t peer_window;
    uint8_t  rcv_buf[1460];
    int      rcv_len;
    uint32_t isn;
} tcp_sock_t;

int  tcp_init(void);
int  tcp_connect(uint32_t ip, uint16_t port, int timeout_ms);
int  tcp_send(int sock, const void *data, uint16_t len, int timeout_ms);
int  tcp_recv(int sock, void *buf, uint16_t max_len, int timeout_ms);
void tcp_close(int sock);
void tcp_handle_packet(uint8_t *pkt, uint16_t len);

#endif
