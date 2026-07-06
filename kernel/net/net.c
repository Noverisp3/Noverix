#include "net.h"
#include "../drivers/rtl8139.h"
#include "../lib.h"
#include "../drivers/serial.h"
#include "../cpu/timer.h"

/* Forward declaration for self-use */
static int net_parse_packet(uint8_t *pkt, uint16_t len);

/* Own MAC address */
static uint8_t own_mac[6];

/* ARP cache: single entry */
static uint32_t arp_cache_ip;
static uint8_t  arp_cache_mac[6];
static int      arp_cache_valid;

/* ICMP echo reply state */
static volatile int ping_reply_received;
static volatile uint16_t ping_reply_id;
static volatile uint16_t ping_reply_seq;

/* Inbound packet buffer for polling */
static uint8_t pkt_buf[1514];

/* Forward declaration */
static void send_arp_request(uint32_t ip);
static void send_icmp_echo(uint32_t dst_ip, uint16_t id, uint16_t seq);

uint16_t net_checksum(void *data, int len)
{
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)data;
    for (int i = 0; i < len / 2; i++)
        sum += ptr[i];
    if (len & 1)
        sum += ((uint8_t *)data)[len - 1];
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return ~((uint16_t)sum);
}

uint32_t net_ip_aton(const char *str)
{
    uint32_t ip = 0;
    int val = 0;
    while (*str) {
        if (*str == '.') {
            ip = (ip << 8) | (val & 0xFF);
            val = 0;
        } else if (*str >= '0' && *str <= '9') {
            val = val * 10 + (*str - '0');
        }
        str++;
    }
    ip = (ip << 8) | (val & 0xFF);
    return ip;
}

int net_init(void)
{
    if (rtl8139_init() < 0) return -1;
    rtl8139_get_mac(own_mac);

    serial_write_string("[net] own IP: 10.0.2.15\n");
    serial_write_string("[net] gateway: 10.0.2.2\n");

    arp_cache_valid = 0;
    ping_reply_received = 0;

    return 0;
}

/* ---- ARP ---- */

static void send_arp_request(uint32_t ip)
{
    uint8_t pkt[42];  /* ethernet(14) + arp(28) = 42 */
    eth_hdr_t *eth = (eth_hdr_t *)pkt;
    arp_pkt_t *arp = (arp_pkt_t *)(pkt + 14);

    /* Ethernet: broadcast */
    lib_memset(eth->dst, 0xFF, 6);
    lib_memcpy(eth->src, own_mac, 6);
    eth->type = net_htons(ETH_ARP);

    /* ARP request */
    arp->htype = net_htons(1);    /* Ethernet */
    arp->ptype = net_htons(ETH_IP);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = net_htons(1);    /* Request */
    lib_memcpy(arp->sha, own_mac, 6);
    arp->spa   = net_htonl(NET_HOST_IP);
    lib_memset(arp->tha, 0, 6);   /* Unknown */
    arp->tpa   = net_htonl(ip);

    rtl8139_send(pkt, sizeof(pkt));
    serial_write_string("[net] ARP request sent\n");
}

static void handle_arp(uint8_t *pkt, uint16_t len)
{
    (void)len;
    arp_pkt_t *arp = (arp_pkt_t *)(pkt + 14);

    uint32_t spa = arp->spa;
    uint8_t *sha = arp->sha;
    uint16_t oper = net_ntohs(arp->oper);

    /* Only cache ARP replies to avoid cache poisoning from unsolicited requests.
     * A reply (oper=2) targeted at our IP address is a legitimate response. */
    if (oper == 2 && arp->tpa == net_htonl(NET_HOST_IP)) {
        arp_cache_ip = net_ntohl(spa);
        lib_memcpy(arp_cache_mac, sha, 6);
        arp_cache_valid = 1;

        serial_write_string("[net] ARP: cached ");
        serial_write_hex(spa);
        serial_write_string(" -> ");
        for (int i = 0; i < 6; i++) {
            serial_write_hex(sha[i]);
            if (i < 5) serial_write_char(':');
        }
        serial_write_char('\n');
    }

    /* If this is a request for us, send reply */
    if (oper == 1) {  /* Request */
        uint32_t target_ip = net_htonl(NET_HOST_IP);
        if (arp->tpa == target_ip) {
            uint8_t reply[42];
            eth_hdr_t *re = (eth_hdr_t *)reply;
            arp_pkt_t *ra = (arp_pkt_t *)(reply + 14);

            lib_memcpy(re->dst, sha, 6);
            lib_memcpy(re->src, own_mac, 6);
            re->type = net_htons(ETH_ARP);

            ra->htype = net_htons(1);
            ra->ptype = net_htons(ETH_IP);
            ra->hlen  = 6;
            ra->plen  = 4;
            ra->oper  = net_htons(2);  /* Reply */
            lib_memcpy(ra->sha, own_mac, 6);
            ra->spa   = target_ip;
            lib_memcpy(ra->tha, sha, 6);
            ra->tpa   = spa;

            rtl8139_send(reply, sizeof(reply));
        }
    }
}

/* ---- ICMP ---- */

static void send_icmp_echo(uint32_t dst_ip, uint16_t id, uint16_t seq)
{
    uint8_t pkt[98];  /* eth(14) + ip(20) + icmp(8) + data(56) */
    eth_hdr_t *eth = (eth_hdr_t *)pkt;
    ip_hdr_t  *ip  = (ip_hdr_t  *)(pkt + 14);
    icmp_hdr_t *icmp = (icmp_hdr_t *)(pkt + 34);

    uint16_t ip_total = 20 + 8 + 56;  /* IP hdr + ICMP hdr + data */

    /* Ethernet */
    lib_memcpy(eth->dst, arp_cache_mac, 6);
    lib_memcpy(eth->src, own_mac, 6);
    eth->type = net_htons(ETH_IP);

    /* IP header */
    ip->ver_ihl    = 0x45;
    ip->dscp_ecn   = 0;
    ip->total_len  = net_htons(ip_total);
    ip->id         = net_htons(0x0001);
    ip->flags_frag = 0;
    ip->ttl        = 64;
    ip->protocol   = IP_ICMP;
    ip->checksum   = 0;
    ip->src_ip     = net_htonl(NET_HOST_IP);
    ip->dst_ip     = net_htonl(dst_ip);
    ip->checksum   = net_checksum(ip, 20);

    /* ICMP echo request */
    icmp->type     = ICMP_ECHO_REQ;
    icmp->code     = 0;
    icmp->checksum = 0;
    icmp->id       = net_htons(id);
    icmp->seq      = net_htons(seq);

    /* Fill data with padding */
    uint8_t *data = pkt + 42;
    for (int i = 0; i < 56; i++)
        data[i] = i;

    icmp->checksum = net_checksum(icmp, 8 + 56);

    rtl8139_send(pkt, 14 + ip_total);
    serial_write_string("[net] ICMP echo request sent\n");
}

static int check_icmp_checksum(icmp_hdr_t *icmp, unsigned total_len)
{
    uint16_t saved = icmp->checksum;
    icmp->checksum = 0;
    uint16_t calc = net_checksum(icmp, total_len);
    icmp->checksum = saved;
    return saved == calc;
}

static void handle_icmp(uint8_t *pkt, uint16_t len)
{
    (void)len;
    unsigned ip_hlen = (pkt[14] & 0x0F) * 4;
    if (ip_hlen < 20) return;
    icmp_hdr_t *icmp = (icmp_hdr_t *)(pkt + 14 + ip_hlen);
    unsigned icmp_len = len - 14 - ip_hlen;
    if (icmp_len < 8) return;
    if (!check_icmp_checksum(icmp, icmp_len)) return;

    if (icmp->type == ICMP_ECHO_REP) {
        uint16_t id  = net_ntohs(icmp->id);
        uint16_t seq = net_ntohs(icmp->seq);

        /* Check if this matches our pending ping */
        if (1) {  /* Accept any for simplicity */
            ping_reply_id = id;
            ping_reply_seq = seq;
            ping_reply_received = 1;

            serial_write_string("[net] ICMP echo reply: id=");
            serial_write_hex(id);
            serial_write_string(" seq=");
            serial_write_hex(seq);
            serial_write_char('\n');
        }
    }
}

/* ---- Packet dispatch ---- */

static int net_parse_packet(uint8_t *pkt, uint16_t len)
{
    if (len < 14) return -1;

    eth_hdr_t *eth = (eth_hdr_t *)pkt;
    uint16_t type = net_ntohs(eth->type);

    switch (type) {
    case ETH_ARP:
        /* Minimum ARP size = 14 + 28 = 42 */
        if (len >= 42) handle_arp(pkt, len);
        return 0;
    case ETH_IP:
        if (len >= 34) {
            ip_hdr_t *ip = (ip_hdr_t *)(pkt + 14);
            unsigned ip_hlen = (ip->ver_ihl & 0x0F) * 4;
            /* Validate: IPv4, valid header length, total length matches,
             * no fragments, valid checksum */
            if ((ip->ver_ihl >> 4) != 4) return 0;
            if (ip_hlen < 20) return 0;
            if (len < 14 + ip_hlen) return 0;
            uint16_t ip_total = net_ntohs(ip->total_len);
            if (ip_total < ip_hlen || len < 14 + ip_total) return 0;
            if ((ip->flags_frag & 0x3F) != 0) return 0;  /* no fragments */
            if (net_checksum(ip, ip_hlen) != 0) return 0;
            if (ip->protocol == IP_ICMP && len >= 14 + ip_hlen + 8)
                handle_icmp(pkt, len);
        }
        return 0;
    default:
        return -1;
    }
}

/* ---- Public API ---- */

int net_arp_resolve(uint32_t ip, uint8_t mac[6])
{
    /* Check cache first */
    if (arp_cache_valid && arp_cache_ip == ip) {
        lib_memcpy(mac, arp_cache_mac, 6);
        return 0;
    }

    /* Send ARP request */
    send_arp_request(ip);

    /* Poll for ARP reply with timeout (~500ms) */
    uint32_t start = get_ticks();
    while ((get_ticks() - start) < 50) {  /* 50 ticks = 500ms */
        uint16_t len;
        if (rtl8139_recv(pkt_buf, &len) == 0)
            net_parse_packet(pkt_buf, len);

        if (arp_cache_valid && arp_cache_ip == ip) {
            lib_memcpy(mac, arp_cache_mac, 6);
            return 0;
        }

        /* Brief delay to let scheduler run other tasks */
        for (volatile int d = 0; d < 10000; d++);
    }

    return -1;  /* Timeout */
}

int net_ping(uint32_t dst_ip, int timeout_ms, int *rtt_ms)
{
    uint8_t gw_mac[6];

    /* Determine target for ARP resolution */
    uint32_t arp_target = dst_ip;
    /* If destination is off-subnet, use the gateway */
    if ((dst_ip & NET_NETMASK) != (NET_HOST_IP & NET_NETMASK))
        arp_target = NET_GW_IP;

    /* Resolve MAC via ARP */
    if (net_arp_resolve(arp_target, gw_mac) < 0) {
        serial_write_string("[ping] ARP failed, retrying...\n");
        if (net_arp_resolve(arp_target, gw_mac) < 0) {
            serial_write_string("[ping] ARP failed\n");
            return -1;
        }
    }

    /* Update ARP cache with the resolved target IP, not dst_ip.
     * For off-subnet pings, arp_target is the gateway; storing
     * dst_ip → gateway_mac would poison future lookups. */
    lib_memcpy(arp_cache_mac, gw_mac, 6);
    arp_cache_ip = arp_target;
    arp_cache_valid = 1;

    /* Reset reply flag */
    ping_reply_received = 0;

    /* Send ICMP echo request */
    uint16_t ping_id = 0x1234;
    uint16_t ping_seq = 1;
    uint32_t start_tick = get_ticks();
    send_icmp_echo(dst_ip, ping_id, ping_seq);

    /* Poll for reply with timeout */
    uint32_t timeout_ticks = (timeout_ms + 9) / 10;
    while ((get_ticks() - start_tick) < timeout_ticks) {
        uint16_t len;
        if (rtl8139_recv(pkt_buf, &len) == 0)
            net_parse_packet(pkt_buf, len);

        if (ping_reply_received) {
            uint32_t now = get_ticks();
            int elapsed = (int)((now - start_tick) * 10);
            if (rtt_ms) *rtt_ms = elapsed;
            return 0;
        }
    }

    return -1;  /* Timeout */
}
