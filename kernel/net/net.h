#ifndef NET_H
#define NET_H

#include <stdint.h>

/* Ethernet types */
#define ETH_ARP  0x0806
#define ETH_IP   0x0800

/* IP protocols */
#define IP_ICMP  1
#define IP_TCP   6

/* ICMP types */
#define ICMP_ECHO_REQ 8
#define ICMP_ECHO_REP 0

/* Default QEMU user-mode network config */
#define NET_GW_IP    ((10 << 24) | (0 << 16) | (2 << 8) | 2)   /* 10.0.2.2 */
#define NET_HOST_IP  ((10 << 24) | (0 << 16) | (2 << 8) | 15)  /* 10.0.2.15 */
#define NET_NETMASK  ((255 << 24) | (255 << 16) | (255 << 8) | 0) /* 255.255.255.0 */

#pragma pack(push, 1)

typedef struct {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;
} eth_hdr_t;

typedef struct {
    uint16_t htype;    /* 1 = Ethernet */
    uint16_t ptype;    /* 0x0800 = IPv4 */
    uint8_t  hlen;     /* 6 */
    uint8_t  plen;     /* 4 */
    uint16_t oper;     /* 1 = request, 2 = reply */
    uint8_t  sha[6];   /* sender MAC */
    uint32_t spa;      /* sender IP (network order) */
    uint8_t  tha[6];   /* target MAC */
    uint32_t tpa;      /* target IP (network order) */
} arp_pkt_t;

typedef struct {
    uint8_t  ver_ihl;      /* 0x45 = IPv4, IHL=5 */
    uint8_t  dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;       /* network order */
    uint32_t dst_ip;       /* network order */
} ip_hdr_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_hdr_t;

#pragma pack(pop)

/* Own MAC address — filled by net_init, used by TCP */
extern uint8_t own_mac[6];

/* Byte-order helpers */
static inline uint16_t net_htons(uint16_t x)
{
    return __builtin_bswap16(x);
}

static inline uint32_t net_htonl(uint32_t x)
{
    return __builtin_bswap32(x);
}

#define net_ntohs net_htons
#define net_ntohl net_htonl

/* IP string (e.g. "10.0.2.2") to uint32 in network byte order */
uint32_t net_ip_aton(const char *str);

/* Checksum (for IP header, ICMP, etc.) */
uint16_t net_checksum(void *data, int len);

/* Initialize networking */
int net_init(void);

/* Resolve IP to MAC via ARP */
int net_arp_resolve(uint32_t ip, uint8_t mac[6]);

/* Send ICMP echo request and wait for reply.
 * Returns 0 on success with rtt_ms set, or -1 on timeout. */
int net_ping(uint32_t dst_ip, int timeout_ms, int *rtt_ms);

#endif
