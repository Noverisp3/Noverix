#include "tcp.h"
#include "net.h"
#include "../drivers/rtl8139.h"
#include "../drivers/serial.h"
#include "../cpu/timer.h"
#include "../lib.h"

static tcp_sock_t sockets[TCP_MAX_SOCKS];
static uint16_t next_ephemeral = 40000;

int tcp_init(void)
{
    lib_memset(sockets, 0, sizeof(sockets));
    serial_write_string("[tcp] init\n");
    return 0;
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                             tcp_hdr_t *tcp, uint16_t tcp_len)
{
    uint32_t sum = 0;
    uint16_t *ptr;

    uint8_t pseudo[12];
    *(uint32_t *)&pseudo[0] = src_ip;
    *(uint32_t *)&pseudo[4] = dst_ip;
    pseudo[8] = 0;
    pseudo[9] = 6;
    *(uint16_t *)&pseudo[10] = net_htons(tcp_len);

    ptr = (uint16_t *)pseudo;
    for (int i = 0; i < 6; i++)
        sum += ptr[i];

    ptr = (uint16_t *)tcp;
    int len = tcp_len;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len)
        sum += *(uint8_t *)ptr;

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return ~((uint16_t)sum);
}

static tcp_sock_t *find_sock_by_port(uint16_t dst_port)
{
    for (int i = 0; i < TCP_MAX_SOCKS; i++) {
        if (sockets[i].used && sockets[i].src_port == dst_port)
            return &sockets[i];
    }
    return 0;
}

static int send_tcp_segment(tcp_sock_t *sock, uint8_t flags,
                            const void *data, uint16_t data_len)
{
    uint8_t pkt[1514];
    eth_hdr_t *eth = (eth_hdr_t *)pkt;
    ip_hdr_t  *ip  = (ip_hdr_t  *)(pkt + 14);
    tcp_hdr_t *tcp = (tcp_hdr_t *)(pkt + 34);

    uint16_t tcp_len = 20 + data_len;
    uint16_t ip_total = 20 + tcp_len;

    lib_memset(eth->dst, 0xFF, 6);
    lib_memcpy(eth->src, own_mac, 6);
    eth->type = net_htons(ETH_IP);

    ip->ver_ihl    = 0x45;
    ip->dscp_ecn   = 0;
    ip->total_len  = net_htons(ip_total);
    ip->id         = net_htons(0x0002);
    ip->flags_frag = 0;
    ip->ttl        = 64;
    ip->protocol   = 6;
    ip->checksum   = 0;
    ip->src_ip     = net_htonl(NET_HOST_IP);
    ip->dst_ip     = net_htonl(sock->dst_ip);
    ip->checksum   = net_checksum(ip, 20);

    tcp->src_port = net_htons(sock->src_port);
    tcp->dst_port = net_htons(sock->dst_port);
    tcp->seq      = net_htonl(sock->snd_nxt);
    tcp->ack      = net_htonl(sock->rcv_nxt);
    tcp->offset_reserved = (5 << 4);
    tcp->flags    = flags;
    tcp->window   = net_htons(1460);
    tcp->urgent   = 0;

    if (data_len > 0)
        lib_memcpy(pkt + 14 + 20 + 20, data, data_len);

    uint32_t src_ip_raw = net_htonl(NET_HOST_IP);
    uint32_t dst_ip_raw = net_htonl(sock->dst_ip);
    tcp->checksum  = 0;
    tcp->checksum  = tcp_checksum(src_ip_raw, dst_ip_raw, tcp, tcp_len);

    /* Resolve MAC via ARP */
    uint8_t mac[6];
    if (net_arp_resolve(sock->dst_ip, mac) < 0)
        return -1;
    lib_memcpy(eth->dst, mac, 6);

    return rtl8139_send(pkt, 14 + ip_total);
}

void tcp_handle_packet(uint8_t *pkt, uint16_t len)
{
    (void)len;
    unsigned ip_hlen = (pkt[14] & 0x0F) * 4;
    if (ip_hlen < 20) return;
    unsigned tcp_off = 14 + ip_hlen;
    if (len < tcp_off + 20) return;

    ip_hdr_t *ip = (ip_hdr_t *)(pkt + 14);
    uint16_t ip_total = net_ntohs(ip->total_len);
    if (14 + ip_total > len) return;

    tcp_hdr_t *tcp = (tcp_hdr_t *)(pkt + tcp_off);
    uint16_t dst_port = net_ntohs(tcp->dst_port);
    tcp_sock_t *sock = find_sock_by_port(dst_port);
    if (!sock) return;

    uint16_t tcp_hlen = ((tcp->offset_reserved >> 4) & 0x0F) * 4;
    if (tcp_hlen < 20 || 14 + ip_total < tcp_off + tcp_hlen) return;

    uint32_t seq = net_ntohl(tcp->seq);
    uint32_t ack = net_ntohl(tcp->ack);
    uint8_t flags = tcp->flags;

    serial_write_string("[tcp] rx port=");
    serial_write_hex(dst_port);
    serial_write_string(" flags=");
    serial_write_hex(flags);
    serial_write_string(" seq=");
    serial_write_hex(seq);
    serial_write_string(" ack=");
    serial_write_hex(ack);
    serial_write_char('\n');

    if (flags & TCP_FLAG_RST) {
        sock->state = TCP_CLOSED;
        serial_write_string("[tcp] RST received\n");
        return;
    }

    if (sock->state == TCP_SYN_SENT) {
        if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
            if (ack != sock->snd_nxt) return;
            sock->rcv_nxt = seq + 1;
            sock->snd_una = ack;
            sock->peer_window = net_ntohs(tcp->window);
            sock->state = TCP_ESTABLISHED;
            send_tcp_segment(sock, TCP_FLAG_ACK, 0, 0);
            serial_write_string("[tcp] connection established\n");
        }
        return;
    }

    if (flags & TCP_FLAG_ACK) {
        if (seq == sock->rcv_nxt - 1 && (flags & TCP_FLAG_SYN)) {
            /* Handshake ACK for SYN-ACK already handled above */
        }
        sock->snd_una = ack;
        sock->peer_window = net_ntohs(tcp->window);
    }

    if (flags & TCP_FLAG_FIN) {
        sock->rcv_nxt = seq + 1;
        send_tcp_segment(sock, TCP_FLAG_ACK, 0, 0);
        if (sock->state == TCP_FIN_WAIT_2) {
            sock->state = TCP_TIME_WAIT;
            send_tcp_segment(sock, TCP_FLAG_ACK, 0, 0);
        } else {
            sock->state = TCP_CLOSE_WAIT;
        }
        serial_write_string("[tcp] FIN received\n");
        return;
    }

    /* Use IP total_len to determine data length, not raw frame length.
     * This avoids interpreting Ethernet padding as TCP data. */
    int data_len = (14 + ip_total) - tcp_off - tcp_hlen;
    if (data_len > 0 || (flags & TCP_FLAG_PSH)) {
        unsigned data_off = tcp_off + tcp_hlen;
        if (data_len > 0 && seq == sock->rcv_nxt) {
            if (data_len > (int)sizeof(sock->rcv_buf))
                data_len = sizeof(sock->rcv_buf);
            lib_memcpy(sock->rcv_buf, pkt + data_off, data_len);
            sock->rcv_len = data_len;
            sock->rcv_nxt = seq + data_len;
            send_tcp_segment(sock, TCP_FLAG_ACK, 0, 0);
            serial_write_string("[tcp] data received len=");
            serial_write_hex(data_len);
            serial_write_char('\n');
        }
    }
}

static int tcp_poll_until(int sock_idx, int target_state, int timeout_ms)
{
    tcp_sock_t *sock = &sockets[sock_idx];
    if (!sock->used) return -1;

    uint32_t start = get_ticks();
    uint32_t timeout_ticks = (timeout_ms + 9) / 10;

    while ((get_ticks() - start) < timeout_ticks) {
        uint16_t len;
        uint8_t buf[1514];
        if (rtl8139_recv(buf, &len) == 0)
            tcp_handle_packet(buf, len);

        if (sock->state == target_state)
            return 0;
        if (sock->state >= TCP_CLOSE_WAIT && target_state != TCP_CLOSED)
            return -1;
        for (volatile int d = 0; d < 1000; d++);
    }
    return -1;
}

int tcp_connect(uint32_t ip, uint16_t port, int timeout_ms)
{
    int idx = -1;
    for (int i = 0; i < TCP_MAX_SOCKS; i++) {
        if (!sockets[i].used) { idx = i; break; }
    }
    if (idx < 0) return -1;

    tcp_sock_t *sock = &sockets[idx];
    lib_memset(sock, 0, sizeof(*sock));
    sock->used = 1;
    sock->dst_ip = ip;
    sock->dst_port = port;
    sock->src_port = next_ephemeral++;
    sock->isn = get_ticks() << 4;

    sock->snd_nxt = sock->isn;
    sock->snd_una = sock->isn;
    sock->state = TCP_SYN_SENT;

    serial_write_string("[tcp] connecting to ");
    serial_write_hex(ip);
    serial_write_string(":");
    serial_write_hex(port);
    serial_write_char('\n');

    if (send_tcp_segment(sock, TCP_FLAG_SYN, 0, 0) < 0) {
        sock->used = 0;
        return -1;
    }
    sock->snd_nxt = sock->isn + 1;

    if (tcp_poll_until(idx, TCP_ESTABLISHED, timeout_ms) < 0) {
        serial_write_string("[tcp] connect timeout\n");
        sock->used = 0;
        return -1;
    }

    return idx;
}

int tcp_send(int sock_idx, const void *data, uint16_t len, int timeout_ms)
{
    tcp_sock_t *sock = &sockets[sock_idx];
    if (!sock->used || sock->state != TCP_ESTABLISHED) return -1;

    if (len > 1460) len = 1460;
    sock->snd_nxt = sock->snd_una;

    if (send_tcp_segment(sock, TCP_FLAG_PSH | TCP_FLAG_ACK, data, len) < 0)
        return -1;

    sock->snd_nxt = sock->snd_una + len;

    uint32_t start = get_ticks();
    uint32_t timeout_ticks = (timeout_ms + 9) / 10;

    while ((get_ticks() - start) < timeout_ticks) {
        uint16_t rlen;
        uint8_t buf[1514];
        if (rtl8139_recv(buf, &rlen) == 0)
            tcp_handle_packet(buf, rlen);
        if (sock->snd_una >= sock->snd_nxt)
            return 0;
        for (volatile int d = 0; d < 1000; d++);
    }
    return -1;
}

int tcp_recv(int sock_idx, void *buf, uint16_t max_len, int timeout_ms)
{
    tcp_sock_t *sock = &sockets[sock_idx];
    if (!sock->used) return -1;

    uint32_t start = get_ticks();
    uint32_t timeout_ticks = (timeout_ms + 9) / 10;

    while ((get_ticks() - start) < timeout_ticks) {
        if (sock->rcv_len > 0) {
            int copy = sock->rcv_len < max_len ? sock->rcv_len : max_len;
            lib_memcpy(buf, sock->rcv_buf, copy);
            if (copy < sock->rcv_len) {
                lib_memcpy(sock->rcv_buf, sock->rcv_buf + copy, sock->rcv_len - copy);
            }
            sock->rcv_len -= copy;
            return copy;
        }

        uint16_t rlen;
        uint8_t pkt_buf[1514];
        if (rtl8139_recv(pkt_buf, &rlen) == 0)
            tcp_handle_packet(pkt_buf, rlen);

        if (sock->state >= TCP_CLOSE_WAIT)
            break;
        for (volatile int d = 0; d < 1000; d++);
    }
    return 0;
}

void tcp_close(int sock_idx)
{
    tcp_sock_t *sock = &sockets[sock_idx];
    if (!sock->used) return;

    if (sock->state == TCP_ESTABLISHED) {
        send_tcp_segment(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
        sock->snd_nxt++;
        sock->state = TCP_FIN_WAIT_1;

        uint32_t start = get_ticks();
        while ((get_ticks() - start) < 50) {
            uint16_t len;
            uint8_t buf[1514];
            if (rtl8139_recv(buf, &len) == 0)
                tcp_handle_packet(buf, len);
            if (sock->state >= TCP_TIME_WAIT || sock->state == TCP_CLOSE_WAIT)
                break;
            for (volatile int d = 0; d < 1000; d++);
        }
    }

    sock->used = 0;
    serial_write_string("[tcp] socket closed\n");
}
