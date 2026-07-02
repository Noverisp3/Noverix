#include "ata.h"
#include "../cpu/ports.h"

#define TIMEOUT 100000

static int ata_exists[2][2];
static char ata_model[2][2][41];

int ata_init(void)
{
    int detected = 0;
    for (int ch = 0; ch < 2; ch++) {
        for (int dr = 0; dr < 2; dr++) {
            unsigned short base = ch ? 0x170 : 0x1F0;
            unsigned short dev = base + 6;
            unsigned short cmd = base + 7;

            outb(dev, 0xE0 | (dr << 4));
            for (int i = 0; i < 4; i++) inb(cmd);

            outb(base + 2, 0);
            outb(base + 3, 0);
            outb(base + 4, 0);
            outb(base + 5, 0);

            outb(cmd, 0xEC);

            unsigned char s = inb(cmd);
            if (s == 0) {
                ata_exists[ch][dr] = 0;
                continue;
            }

            int timeout = TIMEOUT;
            while (timeout--) {
                s = inb(cmd);
                if (s & 1) break;
                if (!(s & 0x80) && (s & 0x08)) break;
            }
            if ((s & 1) || !(s & 0x08)) {
                ata_exists[ch][dr] = 0;
                continue;
            }

            unsigned short buf[256];
            for (int i = 0; i < 256; i++)
                buf[i] = inw(base);

            ata_exists[ch][dr] = 1;
            detected++;

            for (int i = 0; i < 40; i += 2) {
                ata_model[ch][dr][i] = buf[27 + i / 2] >> 8;
                ata_model[ch][dr][i + 1] = buf[27 + i / 2] & 0xFF;
            }
            ata_model[ch][dr][40] = '\0';
            for (int i = 39; i >= 0; i--) {
                if (ata_model[ch][dr][i] == ' ')
                    ata_model[ch][dr][i] = '\0';
                else
                    break;
            }
        }
    }
    return detected;
}

int ata_drive_exists(int channel, int drive)
{
    if (channel < 0 || channel > 1) return 0;
    if (drive < 0 || drive > 1) return 0;
    return ata_exists[channel][drive];
}

const char *ata_get_model(int channel, int drive)
{
    if (!ata_drive_exists(channel, drive)) return "";
    return ata_model[channel][drive];
}

int ata_read_sectors(int channel, int drive, unsigned int lba, unsigned char count, void *buffer)
{
    if (!ata_drive_exists(channel, drive)) return -1;
    unsigned short base = channel ? 0x170 : 0x1F0;

    outb(base + 6, 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    outb(base + 2, count);
    outb(base + 3, lba & 0xFF);
    outb(base + 4, (lba >> 8) & 0xFF);
    outb(base + 5, (lba >> 16) & 0xFF);
    outb(base + 7, 0x20);

    unsigned short *buf = (unsigned short *)buffer;
    for (int s = 0; s < count; s++) {
        unsigned char st;
        int timeout = TIMEOUT;
        do {
            st = inb(base + 7);
            if (st & 1) return -1;
        } while (timeout-- && ((st & 0x80) || !(st & 0x08)));
        if (timeout < 0) return -1;
        for (int i = 0; i < 256; i++)
            buf[s * 256 + i] = inw(base);
    }
    return 0;
}
