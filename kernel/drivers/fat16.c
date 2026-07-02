#include "fat16.h"
#include "ata.h"
#include "screen.h"
#include "../cpu/ports.h"
#include "serial.h"

#define SECTOR_SIZE 512
#define ROOT_ENTRY_SIZE 32

static int mounted;
static unsigned bytes_per_sector;
static unsigned sectors_per_cluster;
static unsigned reserved_sectors;
static unsigned fats;
static unsigned root_entries;
static unsigned sectors_per_fat;
static unsigned total_sectors;
static unsigned root_start;
static unsigned root_sectors;
static unsigned data_start;

static int fat_ch = -1, fat_dr;

static int find_first_drive(void)
{
    for (int ch = 0; ch < 2; ch++)
        for (int dr = 0; dr < 2; dr++)
            if (ata_drive_exists(ch, dr)) return ch * 2 + dr;
    return -1;
}

static void to_83(const char *name, char *out)
{
    for (int i = 0; i < 11; i++) out[i] = ' ';
    out[11] = 0;
    int dot = -1;
    for (int i = 0; name[i]; i++) {
        if (name[i] == '.') { dot = i; break; }
    }
    int name_len = (dot < 0) ? 0 : dot;
    int ext_len = 0;
    if (dot >= 0) {
        for (int j = dot + 1; name[j]; j++) ext_len++;
    }
    if (dot < 0) {
        for (int i = 0; name[i] && i < 8; i++) name_len = i + 1;
    }
    for (int i = 0; i < name_len && i < 8; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[i] = c;
    }
    for (int i = 0; i < ext_len && i < 3; i++) {
        char c = name[dot + 1 + i];
        if (c >= 'a' && c <= 'z') c -= 32;
        out[8 + i] = c;
    }
}

static int read_sector(unsigned lba, unsigned char *dst)
{
    return ata_read_sectors(fat_ch, fat_dr, lba, 1, dst);
}

int fat_mount(void)
{
    unsigned char buf[512];
    int first = find_first_drive();
    if (first < 0) {
        serial_write_string("[fat] no drive\n");
        return -1;
    }
    fat_ch = first / 2;
    fat_dr = first % 2;
    mounted = 0;

    if (read_sector(0, buf) != 0) {
        serial_write_string("[fat] read fail\n");
        return -1;
    }

    bytes_per_sector = buf[0x0B] | (buf[0x0C] << 8);
    sectors_per_cluster = buf[0x0D];
    if (bytes_per_sector == 0 || sectors_per_cluster == 0) {
        serial_write_string("[fat] bad BPB\n");
        return -1;
    }
    reserved_sectors = buf[0x0E] | (buf[0x0F] << 8);
    fats = buf[0x10];
    root_entries = buf[0x11] | (buf[0x12] << 8);
    sectors_per_fat = buf[0x16] | (buf[0x17] << 8);
    total_sectors = buf[0x13] | (buf[0x14] << 8);
    if (total_sectors == 0)
        total_sectors = buf[0x20] | (buf[0x21] << 8) | (buf[0x22] << 16) | (buf[0x23] << 24);

    root_sectors = (root_entries * ROOT_ENTRY_SIZE + bytes_per_sector - 1) / bytes_per_sector;
    root_start = reserved_sectors + fats * sectors_per_fat;
    data_start = root_start + root_sectors;

    mounted = 1;
    return 0;
}

static int find_entry(const char *name83, unsigned *out_off, unsigned *out_cluster, unsigned *out_size)
{
    unsigned char buf[512];
    unsigned sec;
    for (sec = 0; sec < root_sectors; sec++) {
        if (read_sector(root_start + sec, buf) != 0) return -1;
        for (int i = 0; i < (int)(bytes_per_sector / ROOT_ENTRY_SIZE); i++) {
            unsigned off = i * ROOT_ENTRY_SIZE;
            if (buf[off] == 0) return -1;
            if (buf[off] == 0xE5) continue;
            if (buf[off + 0x0B] & 0x08) continue;
            int match = 1;
            for (int j = 0; j < 11; j++) {
                if (buf[off + j] != name83[j]) { match = 0; break; }
            }
            if (match) {
                *out_off = off;
                *out_cluster = buf[off + 0x1A] | (buf[off + 0x1B] << 8);
                *out_size = buf[off + 0x1C] | (buf[off + 0x1D] << 8) | (buf[off + 0x1E] << 16) | (buf[off + 0x1F] << 24);
                return sec;
            }
        }
    }
    return -1;
}

static unsigned next_cluster(unsigned cluster)
{
    unsigned char buf[512];
    unsigned fat_lba = reserved_sectors;
    unsigned fat_off = cluster * 2;
    unsigned fat_sec = fat_lba + fat_off / bytes_per_sector;
    if (read_sector(fat_sec, buf) != 0) return 0xFFF8;
    return buf[fat_off % bytes_per_sector] | (buf[(fat_off % bytes_per_sector) + 1] << 8);
}

int fat_list(void)
{
    if (!mounted) return -1;
    unsigned char buf[512];
    unsigned sec;
    int count = 0;
    for (sec = 0; sec < root_sectors; sec++) {
        if (read_sector(root_start + sec, buf) != 0) return -1;
        for (int i = 0; i < (int)(bytes_per_sector / ROOT_ENTRY_SIZE); i++) {
            unsigned off = i * ROOT_ENTRY_SIZE;
            if (buf[off] == 0) continue;
            if (buf[off] == 0xE5) continue;
            if (buf[off + 0x0B] & 0x08) continue;

            char name[13];
            int k = 0;
            for (int j = 0; j < 8; j++) {
                if (buf[off + j] != ' ') name[k++] = buf[off + j];
            }
            int is_dir = (buf[off + 0x0B] & 0x10) != 0;
            if (!is_dir && buf[off + 8] != ' ') {
                name[k++] = '.';
                for (int j = 8; j < 11; j++) {
                    if (buf[off + j] != ' ') name[k++] = buf[off + j];
                }
            }
            name[k] = 0;

            unsigned size = buf[off + 0x1C] | (buf[off + 0x1D] << 8) | (buf[off + 0x1E] << 16) | (buf[off + 0x1F] << 24);

            print_string("  ");
            if (is_dir) print_string("[DIR] ");
            else print_string("      ");
            print_string(name);
            print_string("  ");
            print_hex(size);
            print_string(is_dir ? " <DIR>\n" : " bytes\n");

            count++;
        }
    }
    serial_write_string("[fat] list: ");
    serial_write_hex(count);
    serial_write_string(" entries\n");
    return 0;
}

int fat_read(const char *name, void *out, unsigned max)
{
    if (!mounted) return -1;
    unsigned char buf[512];
    char name83[12];
    to_83(name, name83);

    unsigned off, cluster, size;
    int sec_idx = find_entry(name83, &off, &cluster, &size);
    if (sec_idx < 0) return -1;

    if (size > max) size = max;
    unsigned char *dst = (unsigned char *)out;
    unsigned remain = size;
    unsigned cur = cluster;

    while (remain > 0 && cur >= 2 && cur < 0xFFF8) {
        unsigned lba = data_start + (cur - 2) * sectors_per_cluster;
        for (unsigned s = 0; s < sectors_per_cluster && remain > 0; s++) {
            if (read_sector(lba + s, buf) != 0) return -1;
            unsigned copy = bytes_per_sector;
            if (copy > remain) copy = remain;
            for (unsigned i = 0; i < copy; i++)
                *dst++ = buf[i];
            remain -= copy;
        }
        cur = next_cluster(cur);
    }
    return size - remain;
}

static int write_sector_raw(unsigned lba, const unsigned char *src)
{
    return ata_write_sectors(fat_ch, fat_dr, lba, 1, src);
}

int fat_write(const char *name, const void *data, unsigned size)
{
    if (!mounted) return -1;
    unsigned char buf[512];
    char name83[12];
    to_83(name, name83);

    unsigned off, cluster, fsize;
    int sec_idx = find_entry(name83, &off, &cluster, &fsize);

    // If file exists, free its clusters and reuse entry
    if (sec_idx >= 0) {
        unsigned cur = cluster;
        while (cur >= 2 && cur < 0xFFF8) {
            unsigned nxt = next_cluster(cur);
            unsigned fat_lba = reserved_sectors;
            unsigned fat_off = cur * 2;
            unsigned fat_sec = fat_lba + fat_off / bytes_per_sector;
            if (read_sector(fat_sec, buf) == 0) {
                unsigned pos = fat_off % bytes_per_sector;
                buf[pos] = 0; buf[pos + 1] = 0;
                write_sector_raw(fat_sec, buf);
                // Mirror to FAT2
                unsigned fat2_sec = fat_sec + sectors_per_fat;
                if (read_sector(fat2_sec, buf) == 0) {
                    buf[pos] = 0; buf[pos + 1] = 0;
                    write_sector_raw(fat2_sec, buf);
                }
            }
            cur = nxt;
        }
    } else {
        // Find free dir entry
        unsigned sec;
        for (sec = 0; sec < root_sectors; sec++) {
            if (read_sector(root_start + sec, buf) != 0) return -1;
            int found = 0;
            for (int i = 0; i < (int)(bytes_per_sector / ROOT_ENTRY_SIZE); i++) {
                unsigned o = i * ROOT_ENTRY_SIZE;
                if (buf[o] == 0 || buf[o] == 0xE5) {
                    off = o;
                    sec_idx = sec;
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        if (sec_idx < 0) return -1;
    }

    // Allocate clusters
    unsigned clusters_needed = (size + sectors_per_cluster * bytes_per_sector - 1) / (sectors_per_cluster * bytes_per_sector);

    // Build cluster chain
    unsigned first_cluster = 0;
    unsigned prev_cluster = 0;
    unsigned allocated = 0;

    if (clusters_needed > 0) {
        unsigned fat_sectors = sectors_per_fat;
        unsigned fat_entries = fat_sectors * bytes_per_sector / 2;

        for (unsigned cl = 2; cl < fat_entries && allocated < clusters_needed; cl++) {
            unsigned fat_lba = reserved_sectors;
            unsigned fat_off = cl * 2;
            unsigned fat_sec = fat_lba + fat_off / bytes_per_sector;
            if (read_sector(fat_sec, buf) != 0) return -1;
            unsigned pos = fat_off % bytes_per_sector;
            unsigned val = buf[pos] | (buf[pos + 1] << 8);
            if (val == 0) {
                if (allocated == clusters_needed - 1) {
                    buf[pos] = FAT16_EOC & 0xFF;
                    buf[pos + 1] = (FAT16_EOC >> 8) & 0xFF;
                } else {
                    buf[pos] = 0x00;
                    buf[pos + 1] = 0x00;
                }
                if (write_sector_raw(fat_sec, buf) != 0) return -1;
                // Mirror FAT2
                unsigned fat2_sec = fat_sec + sectors_per_fat;
                if (read_sector(fat2_sec, buf) != 0) return -1;
                buf[pos] = (allocated == clusters_needed - 1) ? (FAT16_EOC & 0xFF) : 0x00;
                buf[pos + 1] = (allocated == clusters_needed - 1) ? ((FAT16_EOC >> 8) & 0xFF) : 0x00;
                if (write_sector_raw(fat2_sec, buf) != 0) return -1;

                if (first_cluster == 0) first_cluster = cl;

                // Link previous to this
                if (prev_cluster != 0) {
                    unsigned prev_fat_off = prev_cluster * 2;
                    unsigned prev_fat_sec = fat_lba + prev_fat_off / bytes_per_sector;
                    if (read_sector(prev_fat_sec, buf) != 0) return -1;
                    unsigned ppos = prev_fat_off % bytes_per_sector;
                    buf[ppos] = cl & 0xFF; buf[ppos + 1] = (cl >> 8) & 0xFF;
                    if (write_sector_raw(prev_fat_sec, buf) != 0) return -1;
                    // Mirror FAT2
                    unsigned pf2 = prev_fat_sec + sectors_per_fat;
                    if (read_sector(pf2, buf) != 0) return -1;
                    buf[ppos] = cl & 0xFF; buf[ppos + 1] = (cl >> 8) & 0xFF;
                    if (write_sector_raw(pf2, buf) != 0) return -1;
                }

                // Write data to cluster
                unsigned lba = data_start + (cl - 2) * sectors_per_cluster;
                const unsigned char *src = (const unsigned char *)data + allocated * sectors_per_cluster * bytes_per_sector;
                unsigned rem = size - allocated * sectors_per_cluster * bytes_per_sector;
                for (unsigned s = 0; s < sectors_per_cluster; s++) {
                    if (rem == 0) {
                        // Zero fill remaining
                        for (unsigned z = 0; z < bytes_per_sector; z++) buf[z] = 0;
                    } else {
                        unsigned copy = bytes_per_sector;
                        if (copy > rem) copy = rem;
                        for (unsigned z = 0; z < copy; z++) buf[z] = src[s * bytes_per_sector + z];
                        for (unsigned z = copy; z < bytes_per_sector; z++) buf[z] = 0;
                        rem -= copy;
                    }
                    write_sector_raw(lba + s, buf);
                }

                prev_cluster = cl;
                allocated++;
            }
        }
    }

    if (first_cluster == 0 && size > 0) return -1;  // no space

    // Write directory entry
    if (read_sector(root_start + sec_idx, buf) != 0) return -1;
    for (int j = 0; j < 11; j++) buf[off + j] = name83[j];
    buf[off + 0x0B] = 0x20;  // archive
    buf[off + 0x0C] = 0;
    buf[off + 0x0D] = 0;
    buf[off + 0x0E] = 0;
    buf[off + 0x0F] = 0;
    buf[off + 0x10] = 0;
    buf[off + 0x11] = 0;
    buf[off + 0x12] = 0;
    buf[off + 0x13] = 0;
    buf[off + 0x14] = 0;
    buf[off + 0x15] = 0;
    buf[off + 0x16] = 0;
    buf[off + 0x17] = 0;
    buf[off + 0x18] = 0;
    buf[off + 0x19] = 0;
    buf[off + 0x1A] = first_cluster & 0xFF;
    buf[off + 0x1B] = (first_cluster >> 8) & 0xFF;
    buf[off + 0x1C] = size & 0xFF;
    buf[off + 0x1D] = (size >> 8) & 0xFF;
    buf[off + 0x1E] = (size >> 16) & 0xFF;
    buf[off + 0x1F] = (size >> 24) & 0xFF;
    return write_sector_raw(root_start + sec_idx, buf);
}

int fat_delete(const char *name)
{
    if (!mounted) return -1;
    unsigned char buf[512];
    char name83[12];
    to_83(name, name83);

    unsigned off, cluster, size;
    int sec_idx = find_entry(name83, &off, &cluster, &size);
    if (sec_idx < 0) return -1;

    // Free clusters
    unsigned cur = cluster;
    while (cur >= 2 && cur < 0xFFF8) {
        unsigned nxt = next_cluster(cur);
        unsigned fat_lba = reserved_sectors;
        unsigned fat_off = cur * 2;
        unsigned fat_sec = fat_lba + fat_off / bytes_per_sector;
        if (read_sector(fat_sec, buf) == 0) {
            unsigned pos = fat_off % bytes_per_sector;
            buf[pos] = 0; buf[pos + 1] = 0;
            write_sector_raw(fat_sec, buf);
            unsigned fat2_sec = fat_sec + sectors_per_fat;
            if (read_sector(fat2_sec, buf) == 0) {
                buf[pos] = 0; buf[pos + 1] = 0;
                write_sector_raw(fat2_sec, buf);
            }
        }
        cur = nxt;
    }

    // Mark directory entry as deleted
    if (read_sector(root_start + sec_idx, buf) != 0) return -1;
    buf[off] = 0xE5;
    return write_sector_raw(root_start + sec_idx, buf);
}
