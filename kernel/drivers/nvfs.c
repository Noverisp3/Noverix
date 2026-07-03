#include "nvfs.h"
#include "ata.h"
#include "../drivers/screen.h"
#include "../drivers/serial.h"

int nvfs_errno;

static int mounted;
static int nvfs_ch, nvfs_dr;
static unsigned nvfs_cwd;

static unsigned sb_data_start;
static unsigned sb_inode_start;
static unsigned sb_inode_count;
static unsigned sb_inode_blocks;
static unsigned sb_bitmap_start;
static unsigned sb_bitmap_sectors;
static unsigned nvfs_offset;

#define EXTENT_CACHE_MAX (NVFS_DIRECT_EXTENTS + NVFS_INDIRECT_ENTS)

static int find_drive(void)
{
    for (int ch = 0; ch < 2; ch++)
        for (int dr = 0; dr < 2; dr++)
            if (ata_drive_exists(ch, dr)) { nvfs_ch = ch; nvfs_dr = dr; return 0; }
    return -1;
}

static int read_sector(unsigned lba, unsigned char *buf)
{
    return ata_read_sectors(nvfs_ch, nvfs_dr, lba, 1, buf);
}

static int write_sector(unsigned lba, const unsigned char *buf)
{
    return ata_write_sectors(nvfs_ch, nvfs_dr, lba, 1, buf);
}

static int read_block(unsigned block, unsigned char *buf)
{
    return read_sector(nvfs_offset + sb_data_start + block, buf);
}

static int write_block(unsigned block, const unsigned char *buf)
{
    return write_sector(nvfs_offset + sb_data_start + block, buf);
}

static int bitmap_test(unsigned block)
{
    unsigned char buf[NVFS_SECTOR_SIZE];
    unsigned bits_per_sec = NVFS_SECTOR_SIZE * 8;
    unsigned sec = nvfs_offset + sb_bitmap_start + block / bits_per_sec;
    unsigned bit = block % bits_per_sec;
    if (read_sector(sec, buf) != 0) return -1;
    return (buf[bit / 8] >> (bit % 8)) & 1;
}

static int bitmap_set(unsigned block, int used)
{
    unsigned char buf[NVFS_SECTOR_SIZE];
    unsigned bits_per_sec = NVFS_SECTOR_SIZE * 8;
    unsigned sec = nvfs_offset + sb_bitmap_start + block / bits_per_sec;
    unsigned bit = block % bits_per_sec;
    if (read_sector(sec, buf) != 0) return -1;
    if (used) buf[bit / 8] |= (1 << (bit % 8));
    else      buf[bit / 8] &= ~(1 << (bit % 8));
    return write_sector(sec, buf);
}

static int bitmap_find(unsigned count)
{
    unsigned char buf[NVFS_SECTOR_SIZE];
    unsigned bits_per_sec = NVFS_SECTOR_SIZE * 8;
    unsigned total_bits = sb_bitmap_sectors * bits_per_sec;

    for (unsigned s = 0; s < sb_bitmap_sectors; s++) {
        if (read_sector(nvfs_offset + sb_bitmap_start + s, buf) != 0) return -1;
        unsigned base = s * bits_per_sec;
        for (unsigned b = 0; b < bits_per_sec && base + b < total_bits; b++) {
            if (buf[b / 8] & (1 << (b % 8))) continue;
            unsigned start = base + b;
            if (count == 1) return start;
            int ok = 1;
            for (unsigned j = 1; j < count; j++) {
                int r = bitmap_test(start + j);
                if (r != 0) { ok = 0; break; }
            }
            if (ok) return start;
        }
    }
    return -1;
}

static int inode_read(unsigned inum, struct nvfs_inode *inode)
{
    unsigned char buf[NVFS_SECTOR_SIZE];
    unsigned sec = nvfs_offset + sb_inode_start + inum * NVFS_INODE_SIZE / NVFS_SECTOR_SIZE;
    unsigned off = (inum * NVFS_INODE_SIZE) % NVFS_SECTOR_SIZE;
    if (read_sector(sec, buf) != 0) return -1;
    unsigned char *p = buf + off;
    inode->size = *(unsigned int *)(p + 0);
    inode->type = *(p + 4);
    inode->reserved[0] = *(p + 5);
    inode->reserved[1] = *(p + 6);
    inode->reserved[2] = *(p + 7);
    inode->extent_count = *(unsigned int *)(p + 8);
    for (unsigned i = 0; i < NVFS_MAX_EXTENTS; i++) {
        inode->extents[i].start = *(unsigned int *)(p + 12 + i * 8);
        inode->extents[i].count = *(unsigned int *)(p + 12 + i * 8 + 4);
    }
    return 0;
}

static int inode_write(unsigned inum, const struct nvfs_inode *inode)
{
    unsigned char buf[NVFS_SECTOR_SIZE];
    unsigned sec = nvfs_offset + sb_inode_start + inum * NVFS_INODE_SIZE / NVFS_SECTOR_SIZE;
    unsigned off = (inum * NVFS_INODE_SIZE) % NVFS_SECTOR_SIZE;
    if (read_sector(sec, buf) != 0) return -1;
    unsigned char *p = buf + off;
    *(unsigned int *)(p + 0) = inode->size;
    *(p + 4) = inode->type;
    *(p + 5) = inode->reserved[0];
    *(p + 6) = inode->reserved[1];
    *(p + 7) = inode->reserved[2];
    *(unsigned int *)(p + 8) = inode->extent_count;
    for (unsigned i = 0; i < NVFS_MAX_EXTENTS; i++) {
        *(unsigned int *)(p + 12 + i * 8) = inode->extents[i].start;
        *(unsigned int *)(p + 12 + i * 8 + 4) = inode->extents[i].count;
    }
    return write_sector(sec, buf);
}

/* ------------------------------------------------------------------ */
/* Dynamic inode table expansion                                      */
/* ------------------------------------------------------------------ */
static int sb_write_field(unsigned offset, unsigned val)
{
    unsigned char buf[NVFS_SECTOR_SIZE];
    if (read_sector(nvfs_offset + 1, buf) != 0) return -1;
    *(unsigned int *)(buf + offset) = val;
    return write_sector(nvfs_offset + 1, buf);
}

static int expand_inode_table(void)
{
    int blk = bitmap_find(1);
    if (blk < 0) return -1;
    bitmap_set(blk, 1);

    unsigned char zero[NVFS_SECTOR_SIZE];
    for (unsigned k = 0; k < NVFS_SECTOR_SIZE; k++) zero[k] = 0;
    if (write_block(blk, zero) != 0) { bitmap_set(blk, 0); return -1; }

    if (sb_write_field(36, sb_inode_blocks + 1) != 0) { bitmap_set(blk, 0); return -1; }
    sb_inode_blocks++;
    sb_inode_count = sb_inode_blocks * (NVFS_SECTOR_SIZE / NVFS_INODE_SIZE);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Indirect block helpers                                              */
/* ------------------------------------------------------------------ */
static int indirect_read_extents(unsigned lba, struct nvfs_extent *exts, unsigned max)
{
    unsigned char buf[NVFS_SECTOR_SIZE];
    if (read_block(lba, buf) != 0) return -1;
    unsigned count = max;
    if (count > NVFS_INDIRECT_ENTS) count = NVFS_INDIRECT_ENTS;
    for (unsigned i = 0; i < count; i++) {
        exts[i].start = *(unsigned int *)(buf + i * 8);
        exts[i].count = *(unsigned int *)(buf + i * 8 + 4);
    }
    return count;
}

static int indirect_write_extents(unsigned lba, const struct nvfs_extent *exts, unsigned count)
{
    unsigned char buf[NVFS_SECTOR_SIZE];
    for (unsigned k = 0; k < NVFS_SECTOR_SIZE; k++) buf[k] = 0;
    if (count > NVFS_INDIRECT_ENTS) count = NVFS_INDIRECT_ENTS;
    for (unsigned i = 0; i < count; i++) {
        *(unsigned int *)(buf + i * 8) = exts[i].start;
        *(unsigned int *)(buf + i * 8 + 4) = exts[i].count;
    }
    return write_block(lba, buf);
}

static int indirect_alloc(void)
{
    int blk = bitmap_find(1);
    if (blk < 0) return -1;
    unsigned char buf[NVFS_SECTOR_SIZE];
    for (unsigned k = 0; k < NVFS_SECTOR_SIZE; k++) buf[k] = 0;
    if (write_block(blk, buf) != 0) return -1;
    bitmap_set(blk, 1);
    return blk;
}

/* Load all extents (direct + indirect) into a flat array.              */
/* Returns total count on success, or -1 on error.                      */
static int extent_load_all(const struct nvfs_inode *inode,
                           struct nvfs_extent *out, unsigned max)
{
    unsigned direct = inode->extent_count;
    if (direct > NVFS_DIRECT_EXTENTS) direct = NVFS_DIRECT_EXTENTS;
    if (direct > max) direct = max;

    for (unsigned i = 0; i < direct; i++)
        out[i] = inode->extents[i];

    if (inode->extent_count <= NVFS_DIRECT_EXTENTS)
        return inode->extent_count;

    unsigned indirect_count = inode->extent_count - NVFS_DIRECT_EXTENTS;
    if (indirect_count > max - direct)
        indirect_count = max - direct;

    int r = indirect_read_extents(inode->extents[NVFS_DIRECT_EXTENTS].start,
                                  out + direct, indirect_count);
    if (r < 0) return -1;
    return direct + r;
}

/* ------------------------------------------------------------------ */
/* Inode alloc / free                                                  */
/* ------------------------------------------------------------------ */
static int inode_alloc(unsigned char type)
{
    struct nvfs_inode inode;
    for (unsigned i = 1; i < sb_inode_count; i++) {
        if (inode_read(i, &inode) != 0) continue;
        if (inode.type == 0) {
            inode.size = 0;
            inode.type = type;
            inode.extent_count = 0;
            for (int e = 0; e < NVFS_MAX_EXTENTS; e++) {
                inode.extents[e].start = 0;
                inode.extents[e].count = 0;
            }
            if (inode_write(i, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
            return i;
        }
    }

    /* No free inode found — try to expand the inode table */
    if (expand_inode_table() != 0) {
        nvfs_errno = NVFS_ERR_NO_INODE;
        return -1;
    }
    /* Retry: the newly added sector gives us 4 more inodes */
    for (unsigned i = sb_inode_count - (NVFS_SECTOR_SIZE / NVFS_INODE_SIZE);
         i < sb_inode_count; i++) {
        if (inode_read(i, &inode) != 0) continue;
        if (inode.type == 0) {
            inode.size = 0;
            inode.type = type;
            inode.extent_count = 0;
            for (int e = 0; e < NVFS_MAX_EXTENTS; e++) {
                inode.extents[e].start = 0;
                inode.extents[e].count = 0;
            }
            if (inode_write(i, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
            return i;
        }
    }
    nvfs_errno = NVFS_ERR_NO_INODE;
    return -1;
}

static int inode_free(unsigned inum)
{
    if (inum == 0 || inum >= sb_inode_count) return -1;
    struct nvfs_inode inode;
    if (inode_read(inum, &inode) != 0) return -1;

    struct nvfs_extent cache[EXTENT_CACHE_MAX];
    int total = extent_load_all(&inode, cache, EXTENT_CACHE_MAX);
    if (total < 0) return -1;
    for (int i = 0; i < total; i++) {
        for (unsigned j = 0; j < cache[i].count; j++)
            bitmap_set(cache[i].start + j, 0);
    }

    /* Free indirect block if present */
    if (inode.extent_count > NVFS_DIRECT_EXTENTS)
        bitmap_set(inode.extents[NVFS_DIRECT_EXTENTS].start, 0);

    inode.type = 0;
    inode.size = 0;
    inode.extent_count = 0;
    return inode_write(inum, &inode);
}

/* ------------------------------------------------------------------ */
/* Extent operations                                                   */
/* ------------------------------------------------------------------ */
static int extent_read(struct nvfs_inode *inode, unsigned char *buf, unsigned max)
{
    unsigned char tmp[NVFS_SECTOR_SIZE];
    unsigned remain = inode->size;
    if (remain > max) remain = max;
    unsigned pos = 0;

    struct nvfs_extent cache[EXTENT_CACHE_MAX];
    int total = extent_load_all(inode, cache, EXTENT_CACHE_MAX);
    if (total < 0) return -1;

    for (int i = 0; i < total && pos < remain; i++) {
        for (unsigned j = 0; j < cache[i].count && pos < remain; j++) {
            if (read_block(cache[i].start + j, tmp) != 0) return -1;
            unsigned copy = NVFS_SECTOR_SIZE;
            if (copy > remain - pos) copy = remain - pos;
            for (unsigned k = 0; k < copy; k++) buf[pos++] = tmp[k];
        }
    }
    return pos;
}

static int extent_write(struct nvfs_inode *inode, const unsigned char *data, unsigned size)
{
    unsigned needed = (size + NVFS_SECTOR_SIZE - 1) / NVFS_SECTOR_SIZE;
    if (size == 0) { inode->size = 0; inode->extent_count = 0; return 0; }

    int start = bitmap_find(needed);
    if (start < 0) return -1;

    unsigned char tmp[NVFS_SECTOR_SIZE];
    unsigned remain = size;
    unsigned pos = 0;
    for (unsigned j = 0; j < needed; j++) {
        unsigned copy = NVFS_SECTOR_SIZE;
        if (copy > remain) copy = remain;
        for (unsigned k = 0; k < copy; k++) tmp[k] = data[pos++];
        for (unsigned k = copy; k < NVFS_SECTOR_SIZE; k++) tmp[k] = 0;
        if (write_block(start + j, tmp) != 0) return -1;
        bitmap_set(start + j, 1);
    }

    inode->size = size;
    inode->extent_count = 1;
    inode->extents[0].start = start;
    inode->extents[0].count = needed;
    for (unsigned i = 1; i < NVFS_MAX_EXTENTS; i++) {
        inode->extents[i].start = 0;
        inode->extents[i].count = 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Directory operations                                                */
/* ------------------------------------------------------------------ */
static int dir_find(unsigned dir_inode, const char *name)
{
    struct nvfs_inode inode;
    if (inode_read(dir_inode, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    if (inode.type != NVFS_TYPE_DIR) { nvfs_errno = NVFS_ERR_NOT_DIR; return -1; }

    unsigned char tmp[NVFS_SECTOR_SIZE];
    int name_len = 0;
    while (name[name_len]) name_len++;
    if (name_len > NVFS_MAX_NAME) name_len = NVFS_MAX_NAME;

    struct nvfs_extent cache[EXTENT_CACHE_MAX];
    int total = extent_load_all(&inode, cache, EXTENT_CACHE_MAX);
    if (total < 0) { nvfs_errno = NVFS_ERR_IO; return -1; }

    for (int i = 0; i < total; i++) {
        for (unsigned j = 0; j < cache[i].count; j++) {
            if (read_block(cache[i].start + j, tmp) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
            int count = NVFS_SECTOR_SIZE / NVFS_DIRENT_SIZE;
            for (int e = 0; e < count; e++) {
                struct nvfs_dirent *de = (struct nvfs_dirent *)(tmp + e * NVFS_DIRENT_SIZE);
                if (de->inode == 0) continue;
                int match = 1;
                for (int k = 0; k < name_len; k++)
                    if (de->name[k] != name[k]) { match = 0; break; }
                if (match && de->name[name_len] != 0) match = 0;
                if (match) return de->inode;
            }
        }
    }
    nvfs_errno = NVFS_ERR_NOT_FOUND;
    return -1;
}

static int dir_add_extent(struct nvfs_inode *inode, unsigned block)
{
    if (inode->extent_count < NVFS_DIRECT_EXTENTS) {
        unsigned ei = inode->extent_count;
        inode->extents[ei].start = block;
        inode->extents[ei].count = 1;
        inode->extent_count = ei + 1;
        inode->size += NVFS_SECTOR_SIZE;
        return 0;
    }

    /* Direct extents full — use indirect block */
    unsigned ind_lba;
    int need_alloc = 1;
    if (inode->extent_count == NVFS_DIRECT_EXTENTS) {
        ind_lba = indirect_alloc();
        if ((int)ind_lba < 0) { nvfs_errno = NVFS_ERR_NO_SPACE; return -1; }
        inode->extents[NVFS_DIRECT_EXTENTS].start = ind_lba;
        inode->extents[NVFS_DIRECT_EXTENTS].count = NVFS_INDIRECT_MARKER;
    } else {
        ind_lba = inode->extents[NVFS_DIRECT_EXTENTS].start;
        need_alloc = 0;
    }

    unsigned indirect_count = inode->extent_count - NVFS_DIRECT_EXTENTS;
    struct nvfs_extent ibuf[NVFS_INDIRECT_ENTS];
    if (indirect_count > 0) {
        if (indirect_read_extents(ind_lba, ibuf, NVFS_INDIRECT_ENTS) < 0) {
            nvfs_errno = NVFS_ERR_IO;
            if (need_alloc) bitmap_set(ind_lba, 0);
            return -1;
        }
    }

    if (indirect_count >= NVFS_INDIRECT_ENTS) {
        nvfs_errno = NVFS_ERR_NO_SPACE;
        if (need_alloc) bitmap_set(ind_lba, 0);
        return -1;
    }

    ibuf[indirect_count].start = block;
    ibuf[indirect_count].count = 1;
    indirect_count++;

    if (indirect_write_extents(ind_lba, ibuf, indirect_count) != 0) {
        nvfs_errno = NVFS_ERR_IO;
        if (need_alloc) bitmap_set(ind_lba, 0);
        return -1;
    }

    inode->extent_count = NVFS_DIRECT_EXTENTS + indirect_count;
    inode->size += NVFS_SECTOR_SIZE;
    return 0;
}

static int dir_add(unsigned dir_inode, const char *name, unsigned child_inode)
{
    struct nvfs_inode inode;
    if (inode_read(dir_inode, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    if (inode.type != NVFS_TYPE_DIR) { nvfs_errno = NVFS_ERR_NOT_DIR; return -1; }

    unsigned char tmp[NVFS_SECTOR_SIZE];
    int name_len = 0;
    while (name[name_len]) name_len++;
    if (name_len > NVFS_MAX_NAME) name_len = NVFS_MAX_NAME;

    unsigned char nd[32];
    for (int k = 0; k < NVFS_MAX_NAME; k++)
        nd[k] = (k < name_len) ? name[k] : 0;
    nd[NVFS_MAX_NAME] = 0;
    *(unsigned int *)(nd + 28) = child_inode;

    struct nvfs_extent cache[EXTENT_CACHE_MAX];
    int total = extent_load_all(&inode, cache, EXTENT_CACHE_MAX);
    if (total < 0) { nvfs_errno = NVFS_ERR_IO; return -1; }

    /* Try to find a free slot in existing extents */
    for (int i = 0; i < total; i++) {
        for (unsigned j = 0; j < cache[i].count; j++) {
            if (read_block(cache[i].start + j, tmp) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
            int count = NVFS_SECTOR_SIZE / NVFS_DIRENT_SIZE;
            for (int e = 0; e < count; e++) {
                unsigned off = e * NVFS_DIRENT_SIZE;
                if (*(unsigned int *)(tmp + off + 28) == 0) {
                    for (int k = 0; k < 32; k++) tmp[off + k] = nd[k];
                    int ret = write_block(cache[i].start + j, tmp);
                    if (ret != 0) nvfs_errno = NVFS_ERR_IO;
                    return ret;
                }
            }
        }
    }

    /* No free slot — allocate a new block and extent */
    int blk = bitmap_find(1);
    if (blk < 0) { nvfs_errno = NVFS_ERR_NO_SPACE; return -1; }
    unsigned new_block = blk;
    bitmap_set(new_block, 1);

    for (unsigned k = 0; k < NVFS_SECTOR_SIZE; k++) tmp[k] = 0;
    for (int k = 0; k < 32; k++) tmp[k] = nd[k];
    if (write_block(new_block, tmp) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }

    if (dir_add_extent(&inode, new_block) != 0) {
        bitmap_set(new_block, 0);
        return -1;
    }

    int ret = inode_write(dir_inode, &inode);
    if (ret != 0) nvfs_errno = NVFS_ERR_IO;
    return ret;
}

static int dir_remove(unsigned dir_inode, const char *name)
{
    struct nvfs_inode inode;
    if (inode_read(dir_inode, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    if (inode.type != NVFS_TYPE_DIR) { nvfs_errno = NVFS_ERR_NOT_DIR; return -1; }

    unsigned char tmp[NVFS_SECTOR_SIZE];
    int name_len = 0;
    while (name[name_len]) name_len++;
    if (name_len > NVFS_MAX_NAME) name_len = NVFS_MAX_NAME;

    struct nvfs_extent cache[EXTENT_CACHE_MAX];
    int total = extent_load_all(&inode, cache, EXTENT_CACHE_MAX);
    if (total < 0) { nvfs_errno = NVFS_ERR_IO; return -1; }

    for (int i = 0; i < total; i++) {
        for (unsigned j = 0; j < cache[i].count; j++) {
            if (read_block(cache[i].start + j, tmp) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
            int count = NVFS_SECTOR_SIZE / NVFS_DIRENT_SIZE;
            for (int e = 0; e < count; e++) {
                struct nvfs_dirent *de = (struct nvfs_dirent *)(tmp + e * NVFS_DIRENT_SIZE);
                if (de->inode == 0) continue;
                int match = 1;
                for (int k = 0; k < name_len; k++)
                    if (de->name[k] != name[k]) { match = 0; break; }
                if (match && de->name[name_len] != 0) match = 0;
                if (match) {
                    de->inode = 0;
                    int ret = write_block(cache[i].start + j, tmp);
                    if (ret != 0) nvfs_errno = NVFS_ERR_IO;
                    return ret;
                }
            }
        }
    }
    nvfs_errno = NVFS_ERR_NOT_FOUND;
    return -1;
}

static int dir_empty(unsigned dir_inode)
{
    struct nvfs_inode inode;
    if (inode_read(dir_inode, &inode) != 0) return 0;

    struct nvfs_extent cache[EXTENT_CACHE_MAX];
    int total = extent_load_all(&inode, cache, EXTENT_CACHE_MAX);
    if (total < 0) return 0;

    unsigned char tmp[NVFS_SECTOR_SIZE];
    for (int i = 0; i < total; i++) {
        for (unsigned j = 0; j < cache[i].count; j++) {
            if (read_block(cache[i].start + j, tmp) != 0) continue;
            int count = NVFS_SECTOR_SIZE / NVFS_DIRENT_SIZE;
            for (int e = 0; e < count; e++) {
                struct nvfs_dirent *de = (struct nvfs_dirent *)(tmp + e * NVFS_DIRENT_SIZE);
                if (de->inode != 0) return 0;
            }
        }
    }
    return 1;
}

static void to_upper(char *s)
{
    while (*s) {
        if (*s >= 'a' && *s <= 'z') *s -= 32;
        s++;
    }
}

static int find_parent(unsigned inum)
{
    if (inum == NVFS_ROOT_INODE) return -1;
    struct nvfs_inode inode;
    unsigned char tmp[NVFS_SECTOR_SIZE];
    int count = NVFS_SECTOR_SIZE / NVFS_DIRENT_SIZE;

    for (unsigned i = 0; i < sb_inode_count; i++) {
        if (inode_read(i, &inode) != 0) continue;
        if (inode.type != NVFS_TYPE_DIR) continue;

        struct nvfs_extent cache[EXTENT_CACHE_MAX];
        int total = extent_load_all(&inode, cache, EXTENT_CACHE_MAX);
        if (total < 0) continue;

        for (int e = 0; e < total; e++) {
            for (unsigned j = 0; j < cache[e].count; j++) {
                if (read_block(cache[e].start + j, tmp) != 0) continue;
                for (int d = 0; d < count; d++) {
                    struct nvfs_dirent *de = (struct nvfs_dirent *)(tmp + d * NVFS_DIRENT_SIZE);
                    if (de->inode == inum) return i;
                }
            }
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Path resolution                                                     */
/* ------------------------------------------------------------------ */
static int resolve_path(const char *path, unsigned *parent_inode, char *name)
{
    char buf[256];
    int len = 0;
    while (path[len] && len < 255) { buf[len] = path[len]; len++; }
    buf[len] = 0;

    if (len == 0) { *parent_inode = nvfs_cwd; name[0] = 0; return 0; }

    unsigned cur = (buf[0] == '/') ? NVFS_ROOT_INODE : nvfs_cwd;
    int start = (buf[0] == '/') ? 1 : 0;

    int last_slash = -1;
    for (int i = start; i < len; i++)
        if (buf[i] == '/') last_slash = i;

    if (last_slash < 0) {
        int k = 0;
        for (int i = start; i < len; i++) name[k++] = buf[i];
        name[k] = 0;
        if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
            int p = find_parent(cur);
            if (p < 0) { nvfs_errno = NVFS_ERR_PATH; return -1; }
            *parent_inode = p;
            name[0] = 0;
            return 0;
        }
        if (name[0] == '.' && name[1] == 0) {
            name[0] = 0;
            *parent_inode = cur;
            return 0;
        }
        to_upper(name);
        *parent_inode = cur;
        return 0;
    }

    int seg_start = start;
    for (int i = start; i <= last_slash; i++) {
        if (i == last_slash || buf[i] == '/') {
            if (i > seg_start) {
                char comp[256];
                int k = 0;
                for (int j = seg_start; j < i; j++) comp[k++] = buf[j];
                comp[k] = 0;
                if (comp[0] == '.' && comp[1] == '.' && comp[2] == 0) {
                    int p = find_parent(cur);
                    if (p < 0) { nvfs_errno = NVFS_ERR_PATH; return -1; }
                    cur = p;
                } else if (comp[0] == '.' && comp[1] == 0) {
                } else {
                    to_upper(comp);
                    int inum = dir_find(cur, comp);
                    if (inum < 0) { nvfs_errno = NVFS_ERR_NOT_FOUND; return -1; }
                    struct nvfs_inode inode;
                    if (inode_read(inum, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
                    if (inode.type != NVFS_TYPE_DIR) { nvfs_errno = NVFS_ERR_NOT_DIR; return -1; }
                    cur = inum;
                }
            }
            seg_start = i + 1;
        }
    }

    int k = 0;
    for (int i = seg_start; i < len; i++) name[k++] = buf[i];
    name[k] = 0;
    if (name[0] == '.' && name[1] == '.' && name[2] == 0) {
        int p = find_parent(cur);
        if (p < 0) { nvfs_errno = NVFS_ERR_PATH; return -1; }
        *parent_inode = p;
        name[0] = 0;
        return 0;
    }
    if (name[0] == '.' && name[1] == 0) {
        name[0] = 0;
        *parent_inode = cur;
        return 0;
    }
    to_upper(name);
    *parent_inode = cur;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
int nvfs_mount(void)
{
    if (find_drive() != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }

    unsigned char buf[NVFS_SECTOR_SIZE];
    unsigned offsets[] = {0, 2880};

    for (unsigned i = 0; i < 2; i++) {
        nvfs_offset = offsets[i];
        if (read_sector(nvfs_offset + 1, buf) != 0) continue;
        if (buf[0] == 'N' && buf[1] == 'V' && buf[2] == 'F' && buf[3] == 'S') {
            sb_bitmap_start = *(unsigned int *)(buf + 16);
            sb_bitmap_sectors = *(unsigned int *)(buf + 20);
            sb_inode_start = *(unsigned int *)(buf + 24);
            sb_inode_count = *(unsigned int *)(buf + 28);
            sb_data_start = *(unsigned int *)(buf + 32);
            sb_inode_blocks = *(unsigned int *)(buf + 36);
            if (sb_inode_blocks == 0)
                sb_inode_blocks = sb_inode_count / (NVFS_SECTOR_SIZE / NVFS_INODE_SIZE);
            nvfs_cwd = NVFS_ROOT_INODE;
            mounted = 1;
            return 0;
        }
    }
    return -1;
}

int nvfs_list(const char *path)
{
    if (!mounted) { nvfs_errno = NVFS_ERR_NO_MOUNT; return -1; }
    unsigned dir_inode = nvfs_cwd;

    if (path && path[0]) {
        char name[NVFS_MAX_NAME + 1];
        unsigned parent;
        if (resolve_path(path, &parent, name) < 0) {
            print_string("Path not found\n");
            return -1;
        }
        if (name[0]) {
            int inum = dir_find(parent, name);
            if (inum < 0) { print_string("Not found\n"); return -1; }
            struct nvfs_inode inode;
            if (inode_read(inum, &inode) != 0) return -1;
            if (inode.type != NVFS_TYPE_DIR) { print_string("Not a directory\n"); return -1; }
            dir_inode = inum;
        } else {
            dir_inode = parent;
        }
    }

    struct nvfs_inode inode;
    if (inode_read(dir_inode, &inode) != 0) return -1;

    struct nvfs_extent cache[EXTENT_CACHE_MAX];
    int total = extent_load_all(&inode, cache, EXTENT_CACHE_MAX);
    if (total < 0) return -1;

    unsigned char tmp[NVFS_SECTOR_SIZE];
    for (int i = 0; i < total; i++) {
        for (unsigned j = 0; j < cache[i].count; j++) {
            if (read_block(cache[i].start + j, tmp) != 0) return -1;
            int entries = NVFS_SECTOR_SIZE / NVFS_DIRENT_SIZE;
            for (int e = 0; e < entries; e++) {
                struct nvfs_dirent *de = (struct nvfs_dirent *)(tmp + e * NVFS_DIRENT_SIZE);
                if (de->inode == 0) continue;
                int child_inum = de->inode;
                struct nvfs_inode ci;
                const char *type_str = "      ";
                if (inode_read(child_inum, &ci) == 0 && ci.type == NVFS_TYPE_DIR)
                    type_str = "[DIR] ";
                print_string("  ");
                print_string(type_str);
                print_string(de->name);
                print_string("  ");
                print_int(ci.size);
                print_string(ci.type == NVFS_TYPE_DIR ? " <DIR>\n" : " bytes\n");
            }
        }
    }
    return 0;
}

int nvfs_read(const char *path, void *out, unsigned max)
{
    if (!mounted) { nvfs_errno = NVFS_ERR_NO_MOUNT; return -1; }
    char name[NVFS_MAX_NAME + 1];
    unsigned parent;
    if (resolve_path(path, &parent, name) < 0) return -1;
    if (!name[0]) { nvfs_errno = NVFS_ERR_PATH; return -1; }

    int inum = dir_find(parent, name);
    if (inum < 0) return -1;

    struct nvfs_inode inode;
    if (inode_read(inum, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    if (inode.type != NVFS_TYPE_FILE) { nvfs_errno = NVFS_ERR_NOT_FILE; return -1; }

    return extent_read(&inode, (unsigned char *)out, max);
}

int nvfs_write(const char *path, const void *data, unsigned size)
{
    if (!mounted) { nvfs_errno = NVFS_ERR_NO_MOUNT; return -1; }
    char name[NVFS_MAX_NAME + 1];
    unsigned parent;
    if (resolve_path(path, &parent, name) < 0) return -1;
    if (!name[0]) { nvfs_errno = NVFS_ERR_PATH; return -1; }

    int inum = dir_find(parent, name);
    if (inum >= 0) {
        /* Shadow paging: alloc new → write → update inode → free old */
        struct nvfs_inode inode;
        if (inode_read(inum, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }

        /* Save old extent info before extent_write overwrites it */
        unsigned old_count = inode.extent_count;
        struct nvfs_extent old_cache[EXTENT_CACHE_MAX];
        int old_total = extent_load_all(&inode, old_cache, EXTENT_CACHE_MAX);
        if (old_total < 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
        unsigned old_ind_blk = 0;
        if (old_count > NVFS_DIRECT_EXTENTS)
            old_ind_blk = inode.extents[NVFS_DIRECT_EXTENTS].start;

        /* Allocate new extents and write data */
        if (extent_write(&inode, (const unsigned char *)data, size) != 0) {
            nvfs_errno = NVFS_ERR_NO_SPACE;
            return -1;
        }

        /* Persist new inode first — if power fails here, old data is intact */
        if (inode_write(inum, &inode) != 0) {
            nvfs_errno = NVFS_ERR_IO;
            return -1;
        }

        /* Now safe to free old blocks */
        for (int i = 0; i < old_total; i++)
            for (unsigned j = 0; j < old_cache[i].count; j++)
                bitmap_set(old_cache[i].start + j, 0);
        if (old_ind_blk)
            bitmap_set(old_ind_blk, 0);
        return 0;
    }

    /* File does not exist — create new */
    inum = inode_alloc(NVFS_TYPE_FILE);
    if (inum < 0) return -1;

    struct nvfs_inode inode;
    inode.size = 0;
    inode.type = NVFS_TYPE_FILE;
    inode.extent_count = 0;
    if (extent_write(&inode, (const unsigned char *)data, size) != 0) {
        inode_free(inum); nvfs_errno = NVFS_ERR_NO_SPACE; return -1;
    }
    if (inode_write(inum, &inode) != 0) {
        inode_free(inum); nvfs_errno = NVFS_ERR_IO; return -1;
    }
    if (dir_add(parent, name, inum) != 0) { inode_free(inum); return -1; }
    return 0;
}

int nvfs_delete(const char *path)
{
    if (!mounted) { nvfs_errno = NVFS_ERR_NO_MOUNT; return -1; }
    char name[NVFS_MAX_NAME + 1];
    unsigned parent;
    if (resolve_path(path, &parent, name) < 0) return -1;
    if (!name[0]) { nvfs_errno = NVFS_ERR_PATH; return -1; }

    int inum = dir_find(parent, name);
    if (inum < 0) return -1;

    struct nvfs_inode inode;
    if (inode_read(inum, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    if (inode.type != NVFS_TYPE_FILE) { nvfs_errno = NVFS_ERR_NOT_FILE; return -1; }

    if (inode_free(inum) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    return dir_remove(parent, name);
}

int nvfs_mkdir(const char *path)
{
    if (!mounted) { nvfs_errno = NVFS_ERR_NO_MOUNT; return -1; }
    char name[NVFS_MAX_NAME + 1];
    unsigned parent;
    if (resolve_path(path, &parent, name) < 0) return -1;
    if (!name[0]) { nvfs_errno = NVFS_ERR_PATH; return -1; }

    int inum = inode_alloc(NVFS_TYPE_DIR);
    if (inum < 0) return -1;

    struct nvfs_inode inode;
    inode.size = 0;
    inode.type = NVFS_TYPE_DIR;
    inode.extent_count = 0;
    if (inode_write(inum, &inode) != 0) { inode_free(inum); nvfs_errno = NVFS_ERR_IO; return -1; }
    if (dir_add(parent, name, inum) != 0) { inode_free(inum); return -1; }
    return 0;
}

int nvfs_rmdir(const char *path)
{
    if (!mounted) { nvfs_errno = NVFS_ERR_NO_MOUNT; return -1; }
    char name[NVFS_MAX_NAME + 1];
    unsigned parent;
    if (resolve_path(path, &parent, name) < 0) return -1;
    if (!name[0]) { nvfs_errno = NVFS_ERR_PATH; return -1; }

    int inum = dir_find(parent, name);
    if (inum < 0) return -1;

    struct nvfs_inode inode;
    if (inode_read(inum, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    if (inode.type != NVFS_TYPE_DIR) { nvfs_errno = NVFS_ERR_NOT_DIR; return -1; }
    if (!dir_empty(inum)) { nvfs_errno = NVFS_ERR_DIR_BUSY; return -1; }

    if (inode_free(inum) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    return dir_remove(parent, name);
}

int nvfs_chdir(const char *path, unsigned *out_inode)
{
    if (!mounted) { nvfs_errno = NVFS_ERR_NO_MOUNT; return -1; }
    if (!path || !path[0]) { nvfs_cwd = NVFS_ROOT_INODE; if (out_inode) *out_inode = NVFS_ROOT_INODE; return 0; }

    char name[NVFS_MAX_NAME + 1];
    unsigned parent;
    if (resolve_path(path, &parent, name) < 0) return -1;
    if (!name[0]) { nvfs_cwd = parent; if (out_inode) *out_inode = parent; return 0; }

    int inum = dir_find(parent, name);
    if (inum < 0) return -1;

    struct nvfs_inode inode;
    if (inode_read(inum, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    if (inode.type != NVFS_TYPE_DIR) { nvfs_errno = NVFS_ERR_NOT_DIR; return -1; }

    nvfs_cwd = inum;
    if (out_inode) *out_inode = inum;
    return 0;
}

unsigned nvfs_get_cwd(void)
{
    return nvfs_cwd;
}

int nvfs_is_mounted(void)
{
    return mounted;
}

int nvfs_path_string(unsigned inum, char *buf, unsigned size)
{
    if (size < 2) return -1;
    if (inum == NVFS_ROOT_INODE) {
        buf[0] = '/'; buf[1] = 0;
        return 0;
    }

    char components[16][NVFS_MAX_NAME + 1];
    int depth = 0;
    unsigned cur = inum;

    while (cur != NVFS_ROOT_INODE && depth < 16) {
        int parent = find_parent(cur);
        if (parent < 0) break;

        unsigned char tmp[NVFS_SECTOR_SIZE];
        struct nvfs_inode pinode;
        int count = NVFS_SECTOR_SIZE / NVFS_DIRENT_SIZE;
        int found = 0;

        if (inode_read(parent, &pinode) == 0) {
            struct nvfs_extent cache[EXTENT_CACHE_MAX];
            int total = extent_load_all(&pinode, cache, EXTENT_CACHE_MAX);
            if (total > 0) {
                for (int e = 0; e < total && !found; e++) {
                    for (unsigned j = 0; j < cache[e].count && !found; j++) {
                        if (read_block(cache[e].start + j, tmp) != 0) continue;
                        for (int d = 0; d < count; d++) {
                            struct nvfs_dirent *de = (struct nvfs_dirent *)(tmp + d * NVFS_DIRENT_SIZE);
                            if (de->inode == cur) {
                                int k = 0;
                                while (de->name[k] && k < NVFS_MAX_NAME) {
                                    components[depth][k] = de->name[k];
                                    k++;
                                }
                                components[depth][k] = 0;
                                depth++;
                                found = 1;
                                break;
                            }
                        }
                    }
                }
            }
        }
        cur = parent;
    }

    int pos = 0;
    buf[pos++] = '/';
    for (int i = depth - 1; i >= 0 && pos < (int)size - 1; i--) {
        char *s = components[i];
        while (*s && pos < (int)size - 1) buf[pos++] = *s++;
        if (i > 0 && pos < (int)size - 1) buf[pos++] = '/';
    }
    buf[pos] = 0;
    return 0;
}

const char *nvfs_strerror(int err)
{
    switch (err) {
        case NVFS_ERR_NOT_FOUND: return "Not found";
        case NVFS_ERR_NOT_DIR:   return "Not a directory";
        case NVFS_ERR_NOT_FILE:  return "Not a file";
        case NVFS_ERR_DIR_BUSY:  return "Directory not empty";
        case NVFS_ERR_NO_SPACE:  return "Disk full";
        case NVFS_ERR_NO_INODE:  return "Out of inodes";
        case NVFS_ERR_EXISTS:    return "Already exists";
        case NVFS_ERR_IO:        return "I/O error";
        case NVFS_ERR_NO_MOUNT:  return "No disk";
        case NVFS_ERR_PATH:      return "Bad path";
        default:                 return "Unknown error";
    }
}

int nvfs_append(const char *path, const void *data, unsigned size)
{
    if (!mounted) { nvfs_errno = NVFS_ERR_NO_MOUNT; return -1; }

    char name[NVFS_MAX_NAME + 1];
    unsigned parent;
    if (resolve_path(path, &parent, name) < 0) return -1;
    if (!name[0]) { nvfs_errno = NVFS_ERR_PATH; return -1; }

    int inum = dir_find(parent, name);
    if (inum < 0) return -1;

    struct nvfs_inode inode;
    if (inode_read(inum, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    if (inode.type != NVFS_TYPE_FILE) { nvfs_errno = NVFS_ERR_NOT_FILE; return -1; }

    char tmp[4096];
    unsigned old = inode.size > 4096 ? 4096 : inode.size;
    unsigned remain = 4096 - old;
    unsigned add = size > remain ? remain : size;

    if (extent_read(&inode, (unsigned char *)tmp, old) < 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    const unsigned char *src = (const unsigned char *)data;
    for (unsigned i = 0; i < add; i++) tmp[old + i] = src[i];

    /* Shadow paging: save old extent info, alloc+write new, persist inode, free old */
    unsigned old_count = inode.extent_count;
    struct nvfs_extent old_cache[EXTENT_CACHE_MAX];
    int old_total = extent_load_all(&inode, old_cache, EXTENT_CACHE_MAX);
    if (old_total < 0) { nvfs_errno = NVFS_ERR_IO; return -1; }
    unsigned old_ind_blk = 0;
    if (old_count > NVFS_DIRECT_EXTENTS)
        old_ind_blk = inode.extents[NVFS_DIRECT_EXTENTS].start;

    if (extent_write(&inode, (const unsigned char *)tmp, old + add) != 0) {
        nvfs_errno = NVFS_ERR_NO_SPACE; return -1;
    }
    if (inode_write(inum, &inode) != 0) { nvfs_errno = NVFS_ERR_IO; return -1; }

    /* Safe to free old blocks now */
    for (int i = 0; i < old_total; i++)
        for (unsigned j = 0; j < old_cache[i].count; j++)
            bitmap_set(old_cache[i].start + j, 0);
    if (old_ind_blk)
        bitmap_set(old_ind_blk, 0);
    return 0;
}
