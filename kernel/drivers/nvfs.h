#ifndef NVFS_H
#define NVFS_H

#define NVFS_MAGIC     "NVFS"
#define NVFS_SECTOR_SIZE 512
#define NVFS_BLOCK_SIZE  1
#define NVFS_INODE_SIZE  128
#define NVFS_DIRENT_SIZE 32
#define NVFS_MAX_EXTENTS 14
#define NVFS_DIRECT_EXTENTS 13
#define NVFS_EXTENT_SIZE 8
#define NVFS_INDIRECT_ENTS (NVFS_SECTOR_SIZE / NVFS_EXTENT_SIZE)
#define NVFS_INDIRECT_MARKER 0xFFFFFFFF
#define NVFS_MAX_NAME    27
#define NVFS_ROOT_INODE  0

#define NVFS_TYPE_FILE 1
#define NVFS_TYPE_DIR  2
#define NVFS_STATE_CLEAN 0

#define NVFS_ERR_NOT_FOUND  1
#define NVFS_ERR_NOT_DIR    2
#define NVFS_ERR_NOT_FILE   3
#define NVFS_ERR_DIR_BUSY   4
#define NVFS_ERR_NO_SPACE   5
#define NVFS_ERR_NO_INODE   6
#define NVFS_ERR_EXISTS     7
#define NVFS_ERR_IO         8
#define NVFS_ERR_NO_MOUNT   9
#define NVFS_ERR_PATH       10

extern int nvfs_errno;

struct __attribute__((packed)) nvfs_extent {
    unsigned int start;
    unsigned int count;
};

struct __attribute__((packed)) nvfs_inode {
    unsigned int size;
    unsigned char type;
    unsigned char ctime[3];  /* 24-bit creation time (seconds since boot) */
    unsigned int extent_count;
    struct nvfs_extent extents[14];
    unsigned int mtime;      /* 32-bit modification time */
};

struct __attribute__((packed)) nvfs_dirent {
    char name[28];
    unsigned int inode;
};

int nvfs_mount(void);
int nvfs_list(const char *path);
int nvfs_read(const char *path, void *buf, unsigned int max);
int nvfs_write(const char *path, const void *data, unsigned int size);
int nvfs_delete(const char *path);
int nvfs_mkdir(const char *path);
int nvfs_rmdir(const char *path);
int nvfs_chdir(const char *path, unsigned int *out_inode);
unsigned int nvfs_get_cwd(void);
int nvfs_path_string(unsigned int inum, char *buf, unsigned int size);
int nvfs_is_mounted(void);
int nvfs_append(const char *path, const void *data, unsigned int size);
const char *nvfs_strerror(int err);

#endif
