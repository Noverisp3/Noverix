#include "rtl8139.h"
#include "pci.h"
#include "../cpu/ports.h"
#include "../memory/pfa.h"
#include "../lib.h"
#include "../drivers/serial.h"

/* Register offsets from I/O base */
#define RT_IDR0      0x00   /* MAC address (6 bytes) */
#define RT_TX_STATUS 0x10   /* 4 TX status registers (4 bytes each) */
#define RT_TX_ADDR   0x20   /* 4 TX address registers (4 bytes each) */
#define RT_RBSTART   0x30   /* RX buffer start address (32-bit phys) */
#define RT_CR        0x37   /* Command register (8-bit) */
#define RT_CAPR      0x38   /* Current Accepted Packet Read (16-bit) */
#define RT_CBR       0x3A   /* Current Buffer Address (16-bit, read-only) */
#define RT_IMR       0x3C   /* Interrupt Mask (16-bit) */
#define RT_ISR       0x3E   /* Interrupt Status (16-bit) */
#define RT_TX_CONFIG 0x40   /* TX config (32-bit) */
#define RT_RX_CONFIG 0x44   /* RX config (32-bit) */
#define RT_CONFIG1   0x52   /* CONFIG1 (8-bit) */

/* CR bits */
#define CR_RST  0x10
#define CR_RE   0x08
#define CR_TE   0x04

/* RX_CONFIG bits */
#define RCR_AB   (1 << 6)   /* Accept Broadcast */
#define RCR_AM   (1 << 7)   /* Accept Multicast */
#define RCR_APM  (1 << 8)   /* Accept Physical Match */
#define RCR_AAP  (1 << 9)   /* Accept All Packets (promiscuous) */

#define RX_BUF_SIZE  8192
#define TX_NUM       4
#define TX_BUF_SIZE  2048

static uint16_t io_base;
static uint8_t mac[6];
static uint8_t *rx_buf;
static uint8_t *tx_buf_virt[TX_NUM];
static unsigned int tx_buf_phys[TX_NUM];
static int rx_offset;
static int tx_in_use[TX_NUM];

static void rtl8139_reset(void)
{
    outb(io_base + RT_CR, CR_RST);
    int timeout = 0;
    while (inb(io_base + RT_CR) & CR_RST) {
        timeout++;
        if (timeout > 10000) break;
    }
}

int rtl8139_init(void)
{
    serial_write_string("[rtl8139] probing PCI...\n");

    uint16_t dev = pci_find_device(RTL_VENDOR, RTL_DEVICE);
    if (dev == 0xFFFF) {
        serial_write_string("[rtl8139] not found\n");
        return -1;
    }

    uint8_t bus, slot, func;
    pci_unpack(dev, &bus, &slot, &func);

    serial_write_string("[rtl8139] found at ");
    serial_write_hex(dev);
    serial_write_char('\n');

    /* Read I/O BAR (BAR0, offset 0x10) */
    uint32_t bar = pci_read_bar(bus, slot, func, 0);
    io_base = bar & 0xFFFC;  /* Lower bits indicate I/O space */
    serial_write_string("[rtl8139] I/O base=0x");
    serial_write_hex(io_base);
    serial_write_char('\n');

    /* Enable bus master */
    uint32_t cmd = pci_config_read(bus, slot, func, 0x04);
    cmd |= 0x07;  /* I/O + Memory + Bus Master */
    pci_config_write(bus, slot, func, 0x04, cmd);

    /* Power on the chip */
    outb(io_base + RT_CONFIG1, 0x00);

    /* Reset */
    rtl8139_reset();

    /* Read MAC address */
    for (int i = 0; i < 6; i++)
        mac[i] = inb(io_base + RT_IDR0 + i);

    serial_write_string("[rtl8139] MAC ");
    for (int i = 0; i < 6; i++) {
        serial_write_hex(mac[i]);
        if (i < 5) serial_write_char(':');
    }
    serial_write_char('\n');

    /* Allocate RX buffer (2 pages = 8K) */
    rx_buf = (uint8_t *)alloc_frames(2);
    if (!rx_buf) return -1;
    lib_memset(rx_buf, 0, RX_BUF_SIZE);
    outl(io_base + RT_RBSTART, (unsigned int)rx_buf);
    rx_offset = 0;

    /* Allocate TX buffers (4 pages, one per descriptor) */
    for (int i = 0; i < TX_NUM; i++) {
        tx_buf_virt[i] = (uint8_t *)alloc_frame();
        if (!tx_buf_virt[i]) return -1;
        lib_memset(tx_buf_virt[i], 0, TX_BUF_SIZE);
        tx_buf_phys[i] = (unsigned int)tx_buf_virt[i];
        outl(io_base + RT_TX_ADDR + i * 4, tx_buf_phys[i]);
        tx_in_use[i] = 0;
    }

    /* Configure RX: accept broadcast + multicast + physical, 8K+16 buffer */
    outl(io_base + RT_RX_CONFIG, RCR_AB | RCR_AM | RCR_APM | 0x00);

    /* In 8K+16 mode, first 16 bytes are for early RX, data starts at offset 0x10 */
    rx_offset = 0x10;

    /* Configure TX: default */
    outl(io_base + RT_TX_CONFIG, 0x0000);

    /* Unmask TOK and ROK interrupts (not used for polling, but set defaults) */
    outw(io_base + RT_IMR, 0x0005);

    /* Enable RX and TX */
    outb(io_base + RT_CR, CR_RE | CR_TE);

    serial_write_string("[rtl8139] init OK\n");
    return 0;
}

void rtl8139_get_mac(uint8_t out[6])
{
    for (int i = 0; i < 6; i++) out[i] = mac[i];
}

int rtl8139_send(const void *data, uint16_t len)
{
    if (len < 14 || len > 1514) return -1;

    /* Find a free TX descriptor (fire-and-forget, check if busy) */
    int idx = -1;
    for (int i = 0; i < TX_NUM; i++) {
        if (!tx_in_use[i]) { idx = i; break; }
    }
    if (idx < 0) {
        /* All descriptors appear busy — force descriptor 0 */
        idx = 0;
    }

    tx_in_use[idx] = 1;

    /* Copy packet data to TX buffer */
    lib_memcpy(tx_buf_virt[idx], data, len);

    /* Pad to minimum 60 bytes */
    uint16_t tx_len = len < 60 ? 60 : len;

    /* Trigger TX by writing length to TX_STATUS */
    outl(io_base + RT_TX_STATUS + idx * 4, tx_len);

    /* Poll for TX completion (TOK bit = 0x01) */
    for (volatile int timeout = 0; timeout < 500000; timeout++) {
        if (inl(io_base + RT_TX_STATUS + idx * 4) & 0x01) {
            tx_in_use[idx] = 0;
            return 0;
        }
    }

    tx_in_use[idx] = 0;
    return -2;  /* timeout */
}

int rtl8139_recv(uint8_t *buf, uint16_t *len)
{
    /* Check if new data is available by comparing with CBR */
    uint16_t cbr = inw(io_base + RT_CBR);

    while (rx_offset != cbr) {
        /* Read 4-byte RX header at rx_offset */
        uint16_t status = *(volatile uint16_t *)(rx_buf + rx_offset);
        uint16_t pkt_len = *(volatile uint16_t *)(rx_buf + rx_offset + 2);

        if (!(status & 0x0001)) {
            /* ROK not set — packet error, skip */
            rx_offset = (rx_offset + 4 + pkt_len + 3) & ~3;
            if (rx_offset >= RX_BUF_SIZE) rx_offset -= RX_BUF_SIZE;
            goto update_capr;
        }

        /* Remove CRC from length (4 bytes) */
        uint16_t data_len = pkt_len - 4;
        if (data_len > 1514) data_len = 1514;

        /* Copy packet data (skip 4-byte header) */
        rx_offset += 4;
        if (rx_offset + data_len <= RX_BUF_SIZE) {
            lib_memcpy(buf, rx_buf + rx_offset, data_len);
            *len = data_len;
            rx_offset += data_len;
        } else {
            /* Wrap-around */
            uint16_t first = RX_BUF_SIZE - rx_offset;
            lib_memcpy(buf, rx_buf + rx_offset, first);
            lib_memcpy(buf + first, rx_buf, data_len - first);
            rx_offset = data_len - first;
        }

        /* Align to 4-byte boundary for next packet */
        rx_offset = (rx_offset + 3) & ~3;
        if (rx_offset >= RX_BUF_SIZE) rx_offset -= RX_BUF_SIZE;

update_capr: ;
        uint16_t capr_val = rx_offset >= 0x10 ? rx_offset - 0x10 : 0;
        outw(io_base + RT_CAPR, capr_val);
        outw(io_base + RT_ISR, 0x0005);  /* Acknowledge ROK+TOK */
        cbr = inw(io_base + RT_CBR);

        return 0;
    }

    return -1;  /* no packet */
}
