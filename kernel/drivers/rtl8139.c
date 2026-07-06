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

#define RT_PHY_CR    0xE0   /* PHY Command/Address (8-bit) */
#define RT_PHY_DATA  0xE2   /* PHY Data (16-bit) */

/* CR bits (per Linux 8139too) */
#define CR_RST  0x10
#define CR_RE   0x08
#define CR_TE   0x04

/* RX_CONFIG accept bits (per Linux 8139too) */
#define RCR_AAP 0x01    /* Accept All Phys (promiscuous) */
#define RCR_APM 0x02    /* Accept Physical Match */
#define RCR_AM  0x04    /* Accept Multicast */
#define RCR_AB  0x08    /* Accept Broadcast */
#define RCR_ERR 0x20    /* Accept Error */
#define RCR_RUNT 0x10   /* Accept Runt */

/* TX_STATUS bits (per Linux 8139too) */
#define TX_HOST_OWNS 0x2000    /* Bit 13: Host owns descriptor */
#define TX_STAT_OK   0x8000    /* Bit 15: TX completed OK */
#define TX_UNDERRUN  0x4000    /* Bit 14: TX underrun */

/* RX_STATUS bits (per Linux 8139too) */
#define RX_STAT_OK   0x0001    /* Bit 0: RX OK */

/* TxConfig (per Linux 8139too) */
#define TX_IFG96     (3 << 24) /* Interframe gap 9.6us */
#define TX_DMA_BURST (6 << 8)  /* DMA burst */
#define TX_RETRY     (15 << 4) /* TX retry count */

/* RxConfig buffer length bits (per Linux 8139too) */
#define RX_CFG_8K   0
#define RX_CFG_NO_WRAP (1 << 7)
#define RX_CFG_FIFO_NONE (7 << 13)
#define RX_CFG_DMA_UNLIM (7 << 8)

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
static int tx_next_idx;  /* Round-robin to match QEMU's currTxDesc */

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

    /* Allocate RX buffer (3 pages = 12K, enough for 8K+16 mode) */
    rx_buf = (uint8_t *)alloc_frames(3);
    if (!rx_buf) return -1;
    lib_memset(rx_buf, 0, RX_BUF_SIZE);
    outl(io_base + RT_RBSTART, (unsigned int)rx_buf);

    /* Allocate TX buffers (4 pages, one per descriptor) */
    for (int i = 0; i < TX_NUM; i++) {
        tx_buf_virt[i] = (uint8_t *)alloc_frame();
        if (!tx_buf_virt[i]) return -1;
        lib_memset(tx_buf_virt[i], 0, TX_BUF_SIZE);
        tx_buf_phys[i] = (unsigned int)tx_buf_virt[i];
        outl(io_base + RT_TX_ADDR + i * 4, tx_buf_phys[i]);
        /* Mark descriptor as host-owned (bit 13 = TX_HOST_OWNS) */
        outl(io_base + RT_TX_STATUS + i * 4, TX_HOST_OWNS);
        tx_in_use[i] = 0;
    }
    tx_next_idx = 0;

    /* Configure RX: accept broadcast + physical match, 8K, no wrap, DMA unlimited */
    outl(io_base + RT_RX_CONFIG, RX_CFG_8K | RX_CFG_NO_WRAP |
         RX_CFG_FIFO_NONE | RX_CFG_DMA_UNLIM | RCR_AB | RCR_APM);

    /* Configure TX: IFG=96, DMA burst=6, retry=15 */
    outl(io_base + RT_TX_CONFIG, TX_IFG96 | TX_DMA_BURST | TX_RETRY);

    /* Unmask TOK and ROK interrupts (not used for polling, but set defaults) */
    outw(io_base + RT_IMR, 0x0005);

    /* Enable RX and TX */
    outb(io_base + RT_CR, CR_RE | CR_TE);

    /* Verify RX is enabled */
    uint8_t cr_val = inb(io_base + RT_CR);
    uint16_t isr_val = inw(io_base + RT_ISR);
    serial_write_string("[rtl8139] CR=");
    serial_write_hex(cr_val);
    serial_write_string(" ISR=");
    serial_write_hex(isr_val);
    serial_write_char('\n');

    /* Sync with NIC: read CBR so rx_offset matches hardware state */
    rx_offset = inw(io_base + RT_CBR);
    serial_write_string("[rtl8139] CBR=");
    serial_write_hex(rx_offset);
    serial_write_string(" RBSTART=");
    serial_write_hex(inl(io_base + RT_RBSTART));
    serial_write_char('\n');

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

    /* Use round-robin descriptor to match QEMU's currTxDesc progression */
    int idx = tx_next_idx;

    /* Wait for NIC to release ownership of this descriptor before reusing it.
     * TX_HOST_OWNS (bit 13) set = host owns it (NIC is done). */
    {
        volatile int timeout = 5000000;
        while (timeout-- && !(inl(io_base + RT_TX_STATUS + idx * 4) & TX_HOST_OWNS))
            ;
        if (timeout < 0) return -1;
    }

    tx_in_use[idx] = 1;

    /* Copy packet data to TX buffer */
    lib_memcpy(tx_buf_virt[idx], data, len);

    /* Pad to minimum 60 bytes */
    uint16_t tx_len = len < 60 ? 60 : len;

    /* Trigger TX: write length (lower bits) — TxHostOwns (bit 13) cleared = NIC owns */
    outl(io_base + RT_TX_STATUS + idx * 4, tx_len);

    /* Poll for TX completion (TX_STAT_OK = bit 15) */
    uint32_t tx_sts = 0;
    for (volatile int timeout = 0; timeout < 5000000; timeout++) {
        tx_sts = inl(io_base + RT_TX_STATUS + idx * 4);
        if (tx_sts & TX_STAT_OK) {
            tx_in_use[idx] = 0;
            tx_next_idx = (idx + 1) % TX_NUM;
            return 0;
        }
    }

    /* Print detailed status on timeout */
    serial_write_string("[tx] TO ");
    serial_write_hex(tx_sts);
    serial_write_string(" addr=");
    serial_write_hex(inl(io_base + RT_TX_ADDR + idx * 4));
    serial_write_string(" buf[0]=");
    serial_write_hex(((uint8_t*)tx_buf_virt[idx])[0]);
    serial_write_string(" buf[1]=");
    serial_write_hex(((uint8_t*)tx_buf_virt[idx])[1]);
    serial_write_char('\n');
    tx_in_use[idx] = 0;
    return -2;  /* timeout */
}

int rtl8139_recv(uint8_t *buf, uint16_t *len)
{
    /* Check if new data is available by comparing with CBR */
    uint16_t cbr = inw(io_base + RT_CBR);

    { static int cbr_dbg; if (cbr_dbg < 2 && rx_offset != cbr) {
        serial_write_string("[rx] cbr=");
        serial_write_hex(cbr);
        serial_write_string(" off=");
        serial_write_hex(rx_offset);
        serial_write_char('\n');
        cbr_dbg++;
    } }

    while (rx_offset != cbr) {
        /* Read 4-byte RX header at rx_offset */
        uint16_t status = *(volatile uint16_t *)(rx_buf + rx_offset);
        uint16_t pkt_len = *(volatile uint16_t *)(rx_buf + rx_offset + 2);

        /* Debug: print first few RX headers */
        { static int rx_dbg;
          if (rx_dbg < 2) {
              serial_write_string("[rx] off=");
              serial_write_hex(rx_offset);
              serial_write_string(" sts=");
              serial_write_hex(status);
              serial_write_string(" len=");
              serial_write_hex(pkt_len);
              serial_write_char('\n');
              rx_dbg++;
          } }

        /* Validate packet length to avoid integer underflow */
        if (pkt_len < 4 || pkt_len > 0x2000) {
            rx_offset = (rx_offset + 4 + 4 + 3) & ~3;
            rx_offset &= RX_BUF_SIZE - 1;
            goto update_capr;
        }

        if (!(status & RX_STAT_OK)) {
            rx_offset = (rx_offset + 4 + pkt_len + 3) & ~3;
            rx_offset &= RX_BUF_SIZE - 1;
            goto update_capr;
        }

        uint16_t data_len = pkt_len - 4;
        if (data_len > 1514) data_len = 1514;

        /* Copy from data offset (after 4-byte header) */
        unsigned data_off = (rx_offset + 4) & (RX_BUF_SIZE - 1);
        if (data_off + data_len <= RX_BUF_SIZE) {
            lib_memcpy(buf, rx_buf + data_off, data_len);
            *len = data_len;
        } else {
            uint16_t first = RX_BUF_SIZE - data_off;
            lib_memcpy(buf, rx_buf + data_off, first);
            lib_memcpy(buf + first, rx_buf, data_len - first);
            *len = data_len;
        }

        /* Advance past header + full pkt_len (incl CRC), align to 4 */
        rx_offset = (rx_offset + 4 + pkt_len + 3) & ~3;
        rx_offset &= RX_BUF_SIZE - 1;

update_capr: ;
        /* QEMU internally does: RxBufPtr = CAPR + 0x10.
         * Compensate with proper modular arithmetic so NIC's read
         * pointer matches our rx_offset even on wrap-around. */
        outw(io_base + RT_CAPR, (uint16_t)((rx_offset - 16) & (RX_BUF_SIZE - 1)));
        outw(io_base + RT_ISR, 0x0005);
        cbr = inw(io_base + RT_CBR);

        break;
    }

    return -1;  /* no packet */
}
