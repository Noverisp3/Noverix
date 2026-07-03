#include "fat16.h"
#include "ata.h"
#include "screen.h"
#include "../cpu/ports.h"
#include "serial.h"

#define SECTOR_SIZE 512
#define ROOT_ENTRY_SIZE 32
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
#define ATTR_LONG_NAME 0x0F
#define LAST_CLUSTER   0xFFF8

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
static unsigned fat_cwd;

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

static int write_sector_raw(unsigned lba, const unsigned char *src)
{
    return ata_write_sectors(fat_ch, fat_dr, lba, 1, src);
}

static unsigned next_cluster(unsigned cluster)
{
    unsigned char buf[512];
    unsigned fat_lba = reserved_sectors;
    unsigned fat_off = cluster * 2;
    unsigned fat_sec = fat_lba + fat_off / bytes_per_sector;
    if (read_sector(fat_sec, buf) != 0) return LAST_CLUSTER;
    return buf[fat_off % bytes_per_sector] | (buf[(fat_off % bytes_per_sector) + 1] << 8);
}

static int for_each_dir_sector(unsigned dir_cluster,
                               int (*cb)(unsigned lba, unsigned char *buf, void *ctx),
                               void *ctx)
{
    unsigned char buf[512];
    if (dir_cluster == 0) {
        for (unsigned sec = 0; sec < root_sectors; sec++) {
            if (read_sector(root_start + sec, buf) != 0) return -1;
            int r = cb(root_start + sec, buf, ctx);
            if (r) return r;
        }
    } else {
        unsigned cur = dir_cluster;
        while (cur >= 2 && cur < LAST_CLUSTER) {
            unsigned base = data_start + (cur - 2) * sectors_per_cluster;
            for (unsigned s = 0; s < sectors_per_cluster; s++) {
                if (read_sector(base + s, buf) != 0) return -1;
                int r = cb(base + s, buf, ctx);
                if (r) return r;
            }
            cur = next_cluster(cur);
        }
    }
    return 0;
}

struct find_ctx {
    const char *name83;
    unsigned out_off, out_lba, out_cluster, out_size, out_attr;
    int found;
};

static int find_cb(unsigned lba, unsigned char *buf, void *vctx)
{
    struct find_ctx *ctx = (struct find_ctx *)vctx;
    int count = bytes_per_sector / ROOT_ENTRY_SIZE;
    for (int i = 0; i < count; i++) {
        unsigned off = i * ROOT_ENTRY_SIZE;
        if (buf[off] == 0) return 1;
        if (buf[off] == 0xE5) continue;
        if ((buf[off + 0x0B] & ATTR_LONG_NAME) == ATTR_LONG_NAME) continue;
        int match = 1;
        for (int j = 0; j < 11; j++)
            if (buf[off + j] != ctx->name83[j]) { match = 0; break; }
        if (match) {
            ctx->out_off = off;
            ctx->out_lba = lba;
            ctx->out_cluster = buf[off + 0x1A] | (buf[off + 0x1B] << 8);
            ctx->out_size = buf[off + 0x1C] | (buf[off + 0x1D] << 8)
                          | (buf[off + 0x1E] << 16) | (buf[off + 0x1F] << 24);
            ctx->out_attr = buf[off + 0x0B];
            ctx->found = 1;
            return 1;
        }
    }
    return 0;
}

static int find_entry_in(unsigned dir_cluster, const char *name83,
                         unsigned *out_off, unsigned *out_lba,
                         unsigned *out_cluster, unsigned *out_size, unsigned *out_attr)
{
    struct find_ctx ctx;
    ctx.name83 = name83;
    ctx.found = 0;
    int r = for_each_dir_sector(dir_cluster, find_cb, &ctx);
    if (r < 0) return -1;
    if (!ctx.found) return -1;
    if (out_off) *out_off = ctx.out_off;
    if (out_lba) *out_lba = ctx.out_lba;
    if (out_cluster) *out_cluster = ctx.out_cluster;
    if (out_size) *out_size = ctx.out_size;
    if (out_attr) *out_attr = ctx.out_attr;
    return 0;
}

static int resolve_path(const char *path, unsigned *parent_cluster, char *name83)
{
    char buf[256];
    int len = 0;
    while (path[len] && len < 255) { buf[len] = path[len]; len++; }
    buf[len] = 0;

    int last_slash = -1;
    for (int i = 0; i < len; i++)
        if (buf[i] == '/') last_slash = i;

    if (last_slash < 0) {
        to_83(path, name83);
        *parent_cluster = fat_cwd;
        return 0;
    }

    unsigned cluster = (buf[0] == '/') ? 0 : fat_cwd;
    int start = 0;
    for (int i = 0; i <= last_slash; i++) {
        if (i == last_slash || buf[i] == '/') {
            if (i > start) {
                char comp[256], comp83[12];
                int k = 0;
                for (int j = start; j < i; j++) comp[k++] = buf[j];
                comp[k] = 0;
                to_83(comp, comp83);
                unsigned cl, attr;
                if (find_entry_in(cluster, comp83, 0, 0, &cl, 0, &attr) < 0)
                    return -1;
                if (!(attr & ATTR_DIRECTORY)) return -1;
                cluster = cl;
            }
            start = i + 1;
        }
    }

    char leaf[256];
    int k = 0;
    for (int j = last_slash + 1; j < len; j++) leaf[k++] = buf[j];
    leaf[k] = 0;
    to_83(leaf, name83);
    *parent_cluster = cluster;
    return 0;
}

struct free_entry_ctx {
    unsigned out_off, out_lba;
    int found;
};

static int free_entry_cb(unsigned lba, unsigned char *buf, void *vctx)
{
    struct free_entry_ctx *ctx = (struct free_entry_ctx *)vctx;
    int count = bytes_per_sector / ROOT_ENTRY_SIZE;
    for (int i = 0; i < count; i++) {
        unsigned off = i * ROOT_ENTRY_SIZE;
        if (buf[off] == 0 || buf[off] == 0xE5) {
            ctx->out_off = off;
            ctx->out_lba = lba;
            ctx->found = 1;
            return 1;
        }
    }
    return 0;
}

static int find_free_entry_in(unsigned dir_cluster, unsigned *out_off, unsigned *out_lba)
{
    struct free_entry_ctx ctx;
    ctx.found = 0;
    int r = for_each_dir_sector(dir_cluster, free_entry_cb, &ctx);
    if (r < 0) return -1;
    if (!ctx.found) return -1;
    if (out_off) *out_off = ctx.out_off;
    if (out_lba) *out_lba = ctx.out_lba;
    return 0;
}

static void set_entry_fields(unsigned char *buf, unsigned off,
                             const char *name83, unsigned attr,
                             unsigned first_cluster, unsigned size)
{
    for (int j = 0; j < 11; j++) buf[off + j] = name83[j];
    buf[off + 0x0B] = attr;
    for (int j = 0x0C; j < 0x1A; j++) buf[off + j] = 0;
    buf[off + 0x1A] = first_cluster & 0xFF;
    buf[off + 0x1B] = (first_cluster >> 8) & 0xFF;
    buf[off + 0x1C] = size & 0xFF;
    buf[off + 0x1D] = (size >> 8) & 0xFF;
    buf[off + 0x1E] = (size >> 16) & 0xFF;
    buf[off + 0x1F] = (size >> 24) & 0xFF;
}

static int alloc_clusters(unsigned needed, unsigned *first_cluster)
{
    if (needed == 0) { *first_cluster = 0; return 0; }
    unsigned char buf[512];
    unsigned fat_entries = sectors_per_fat * bytes_per_sector / 2;
    unsigned allocated = 0, prev = 0;
    *first_cluster = 0;

    for (unsigned cl = 2; cl < fat_entries && allocated < needed; cl++) {
        unsigned fat_off = cl * 2;
        unsigned fat_sec = reserved_sectors + fat_off / bytes_per_sector;
        if (read_sector(fat_sec, buf) != 0) return -1;
        unsigned pos = fat_off % bytes_per_sector;
        unsigned val = buf[pos] | (buf[pos + 1] << 8);
        if (val != 0) continue;

        unsigned nval = (allocated == needed - 1) ? FAT16_EOC : 0x0000;
        buf[pos] = nval & 0xFF; buf[pos + 1] = (nval >> 8) & 0xFF;
        write_sector_raw(fat_sec, buf);
        unsigned fat2 = fat_sec + sectors_per_fat;
        read_sector(fat2, buf);
        buf[pos] = nval & 0xFF; buf[pos + 1] = (nval >> 8) & 0xFF;
        write_sector_raw(fat2, buf);

        if (*first_cluster == 0) *first_cluster = cl;

        if (prev != 0) {
            unsigned pf = reserved_sectors + (prev * 2) / bytes_per_sector;
            unsigned pp = (prev * 2) % bytes_per_sector;
            read_sector(pf, buf);
            buf[pp] = cl & 0xFF; buf[pp + 1] = (cl >> 8) & 0xFF;
            write_sector_raw(pf, buf);
            unsigned pf2 = pf + sectors_per_fat;
            read_sector(pf2, buf);
            buf[pp] = cl & 0xFF; buf[pp + 1] = (cl >> 8) & 0xFF;
            write_sector_raw(pf2, buf);
        }
        prev = cl;
        allocated++;
    }
    return (allocated < needed) ? -1 : 0;
}

static void free_cluster_chain(unsigned cluster)
{
    unsigned char buf[512];
    unsigned cur = cluster;
    while (cur >= 2 && cur < LAST_CLUSTER) {
        unsigned nxt = next_cluster(cur);
        unsigned fat_off = cur * 2;
        unsigned fat_sec = reserved_sectors + fat_off / bytes_per_sector;
        if (read_sector(fat_sec, buf) == 0) {
            unsigned pos = fat_off % bytes_per_sector;
            buf[pos] = 0; buf[pos + 1] = 0;
            write_sector_raw(fat_sec, buf);
            unsigned fat2 = fat_sec + sectors_per_fat;
            if (read_sector(fat2, buf) == 0) {
                buf[pos] = 0; buf[pos + 1] = 0;
                write_sector_raw(fat2, buf);
            }
        }
        cur = nxt;
    }
}

static int is_dir_empty(unsigned dir_cluster)
{
    unsigned char buf[512];
    unsigned cur = dir_cluster;
    unsigned count = 0;
    while (cur >= 2 && cur < LAST_CLUSTER) {
        unsigned base = data_start + (cur - 2) * sectors_per_cluster;
        for (unsigned s = 0; s < sectors_per_cluster; s++) {
            if (read_sector(base + s, buf) != 0) return 0;
            int cnt = bytes_per_sector / ROOT_ENTRY_SIZE;
            for (int i = 0; i < cnt; i++) {
                unsigned off = i * ROOT_ENTRY_SIZE;
                if (buf[off] == 0) return 1;
                if (buf[off] == 0xE5) continue;
                if ((buf[off + 0x0B] & ATTR_LONG_NAME) == ATTR_LONG_NAME) continue;
                count++;
                if (count > 2) return 0;
            }
        }
        cur = next_cluster(cur);
    }
    return 1;
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
    fat_cwd = 0;
    return 0;
}

int fat_list(const char *path)
{
    if (!mounted) return -1;

    unsigned dir_cluster = 0;
    if (path && path[0]) {
        char name83[12];
        unsigned r = resolve_path(path, &dir_cluster, name83);
        if (r < 0) { print_string("Path not found\n"); return -1; }
        if (name83[0]) {
            unsigned cl, attr;
            if (find_entry_in(dir_cluster, name83, 0, 0, &cl, 0, &attr) < 0) {
                print_string("Not found\n"); return -1;
            }
            if (!(attr & ATTR_DIRECTORY)) {
                print_string("Not a directory\n"); return -1;
            }
            dir_cluster = cl;
        }
    }

    unsigned char buf[512];
    int count = 0;

    if (dir_cluster == 0) {
        for (unsigned sec = 0; sec < root_sectors; sec++) {
            if (read_sector(root_start + sec, buf) != 0) return -1;
            int cnt = bytes_per_sector / ROOT_ENTRY_SIZE;
            for (int i = 0; i < cnt; i++) {
                unsigned off = i * ROOT_ENTRY_SIZE;
                if (buf[off] == 0) continue;
                if (buf[off] == 0xE5) continue;
                if ((buf[off + 0x0B] & ATTR_LONG_NAME) == ATTR_LONG_NAME) continue;
                char name[13]; int k = 0;
                for (int j = 0; j < 8; j++)
                    if (buf[off + j] != ' ') name[k++] = buf[off + j];
                int is_dir = (buf[off + 0x0B] & ATTR_DIRECTORY) != 0;
                if (!is_dir && buf[off + 8] != ' ') {
                    name[k++] = '.';
                    for (int j = 8; j < 11; j++)
                        if (buf[off + j] != ' ') name[k++] = buf[off + j];
                }
                name[k] = 0;
                unsigned size = buf[off + 0x1C] | (buf[off + 0x1D] << 8)
                              | (buf[off + 0x1E] << 16) | (buf[off + 0x1F] << 24);
                print_string("  ");
                print_string(is_dir ? "[DIR] " : "      ");
                print_string(name);
                print_string("  ");
                print_hex(size);
                print_string(is_dir ? " <DIR>\n" : " bytes\n");
                count++;
            }
        }
    } else {
        unsigned cur = dir_cluster;
        while (cur >= 2 && cur < LAST_CLUSTER) {
            unsigned base = data_start + (cur - 2) * sectors_per_cluster;
            for (unsigned s = 0; s < sectors_per_cluster; s++) {
                if (read_sector(base + s, buf) != 0) return -1;
                int cnt = bytes_per_sector / ROOT_ENTRY_SIZE;
                for (int i = 0; i < cnt; i++) {
                    unsigned off = i * ROOT_ENTRY_SIZE;
                    if (buf[off] == 0) break;
                    if (buf[off] == 0xE5) continue;
                    if ((buf[off + 0x0B] & ATTR_LONG_NAME) == ATTR_LONG_NAME) continue;
                    char name[13]; int k = 0;
                    for (int j = 0; j < 8; j++)
                        if (buf[off + j] != ' ') name[k++] = buf[off + j];
                    int is_dir = (buf[off + 0x0B] & ATTR_DIRECTORY) != 0;
                    if (!is_dir && buf[off + 8] != ' ') {
                        name[k++] = '.';
                        for (int j = 8; j < 11; j++)
                            if (buf[off + j] != ' ') name[k++] = buf[off + j];
                    }
                    name[k] = 0;
                    if (name[0] == '.') continue;
                    unsigned size = buf[off + 0x1C] | (buf[off + 0x1D] << 8)
                                  | (buf[off + 0x1E] << 16) | (buf[off + 0x1F] << 24);
                    print_string("  ");
                    print_string(is_dir ? "[DIR] " : "      ");
                    print_string(name);
                    print_string("  ");
                    print_hex(size);
                    print_string(is_dir ? " <DIR>\n" : " bytes\n");
                    count++;
                }
                if (buf[0] == 0) break;
            }
            cur = next_cluster(cur);
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
    unsigned parent_cluster;
    if (resolve_path(name, &parent_cluster, name83) < 0) return -1;

    unsigned cluster, size;
    if (find_entry_in(parent_cluster, name83, 0, 0, &cluster, &size, 0) < 0)
        return -1;

    if (size > max) size = max;
    unsigned char *dst = (unsigned char *)out;
    unsigned remain = size;
    unsigned cur = cluster;

    while (remain > 0 && cur >= 2 && cur < LAST_CLUSTER) {
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

int fat_write(const char *name, const void *data, unsigned size)
{
    if (!mounted) return -1;
    unsigned char buf[512];
    char name83[12];
    unsigned parent_cluster;
    if (resolve_path(name, &parent_cluster, name83) < 0) return -1;

    unsigned off, lba, cluster, fsize;
    int exists = (find_entry_in(parent_cluster, name83, &off, &lba, &cluster, &fsize, 0) == 0);

    if (exists) free_cluster_chain(cluster);
    else if (find_free_entry_in(parent_cluster, &off, &lba) < 0) return -1;

    unsigned cluster_bytes = sectors_per_cluster * bytes_per_sector;
    unsigned needed = (size + cluster_bytes - 1) / cluster_bytes;
    unsigned first_cluster;
    if (alloc_clusters(needed, &first_cluster) < 0 && size > 0) return -1;

    if (size > 0 && first_cluster) {
        unsigned cur = first_cluster;
        unsigned remain = size;
        const unsigned char *src = (const unsigned char *)data;
        while (cur >= 2 && cur < LAST_CLUSTER && remain > 0) {
            unsigned base = data_start + (cur - 2) * sectors_per_cluster;
            for (unsigned s = 0; s < sectors_per_cluster && remain > 0; s++) {
                unsigned copy = bytes_per_sector;
                if (copy > remain) copy = remain;
                for (unsigned z = 0; z < copy; z++) buf[z] = src[z];
                for (unsigned z = copy; z < bytes_per_sector; z++) buf[z] = 0;
                write_sector_raw(base + s, buf);
                src += copy; remain -= copy;
            }
            cur = next_cluster(cur);
        }
    }

    if (read_sector(lba, buf) != 0) return -1;
    set_entry_fields(buf, off, name83, ATTR_ARCHIVE, first_cluster, size);
    return write_sector_raw(lba, buf);
}

int fat_delete(const char *name)
{
    if (!mounted) return -1;
    char name83[12];
    unsigned parent_cluster;
    if (resolve_path(name, &parent_cluster, name83) < 0) return -1;

    unsigned off, lba, cluster, attr;
    if (find_entry_in(parent_cluster, name83, &off, &lba, &cluster, 0, &attr) < 0)
        return -1;
    if (attr & ATTR_DIRECTORY) return -1;

    free_cluster_chain(cluster);

    unsigned char buf[512];
    if (read_sector(lba, buf) != 0) return -1;
    buf[off] = 0xE5;
    return write_sector_raw(lba, buf);
}

int fat_mkdir(const char *path)
{
    if (!mounted) return -1;
    char name83[12];
    unsigned parent_cluster;
    if (resolve_path(path, &parent_cluster, name83) < 0) return -1;
    if (!name83[0]) return -1;

    unsigned off, lba;
    if (find_free_entry_in(parent_cluster, &off, &lba) < 0) return -1;

    unsigned first_cluster;
    if (alloc_clusters(1, &first_cluster) < 0) return -1;

    unsigned char buf[512];
    unsigned base = data_start + (first_cluster - 2) * sectors_per_cluster;
    for (unsigned s = 0; s < sectors_per_cluster; s++) {
        for (unsigned z = 0; z < bytes_per_sector; z++) buf[z] = 0;
        write_sector_raw(base + s, buf);
    }

    char dot[12] = ".          ";
    read_sector(base, buf);
    set_entry_fields(buf, 0, dot, ATTR_DIRECTORY, first_cluster, 0);
    write_sector_raw(base, buf);

    char dotdot[12] = "..         ";
    read_sector(base, buf);
    set_entry_fields(buf, 32, dotdot, ATTR_DIRECTORY, parent_cluster, 0);
    write_sector_raw(base, buf);

    read_sector(lba, buf);
    set_entry_fields(buf, off, name83, ATTR_DIRECTORY, first_cluster, 0);
    return write_sector_raw(lba, buf);
}

int fat_chdir(const char *path, unsigned *out_cluster)
{
    if (!mounted) return -1;
    if (!path || !path[0]) { fat_cwd = 0; if (out_cluster) *out_cluster = 0; return 0; }
    char name83[12];
    unsigned parent_cluster;
    if (resolve_path(path, &parent_cluster, name83) < 0) return -1;
    if (!name83[0]) { fat_cwd = parent_cluster; if (out_cluster) *out_cluster = parent_cluster; return 0; }
    unsigned cl, attr;
    if (find_entry_in(parent_cluster, name83, 0, 0, &cl, 0, &attr) < 0)
        return -1;
    if (!(attr & ATTR_DIRECTORY)) return -1;
    fat_cwd = cl;
    if (out_cluster) *out_cluster = cl;
    return 0;
}

int fat_rmdir(const char *path)
{
    if (!mounted) return -1;
    char name83[12];
    unsigned parent_cluster;
    if (resolve_path(path, &parent_cluster, name83) < 0) return -1;
    if (!name83[0]) return -1;

    unsigned off, lba, cluster, attr;
    if (find_entry_in(parent_cluster, name83, &off, &lba, &cluster, 0, &attr) < 0)
        return -1;
    if (!(attr & ATTR_DIRECTORY)) return -1;
    if (!is_dir_empty(cluster)) return -1;

    free_cluster_chain(cluster);

    unsigned char buf[512];
    if (read_sector(lba, buf) != 0) return -1;
    buf[off] = 0xE5;
    return write_sector_raw(lba, buf);
}
