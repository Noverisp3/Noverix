#ifndef NVFS_H
#define NVFS_H

#define NVFS_MAGIC     "NVFS"
#define NVFS_SECTOR_SIZE 512
#define NVFS_BLOCK_SIZE  1
#define NVFS_INODE_SIZE  128
#define NVFS_DIRENT_SIZE 32
#define NVFS_MAX_EXTENTS 14
#define NVFS_MAX_NAME    27
#define NVFS_ROOT_INODE  0

#define NVFS_TYPE_FILE 1
#define NVFS_TYPE_DIR  2
#define NVFS_STATE_CLEAN 0

struct __attribute__((packed)) nvfs_extent {
    unsigned int start;
    unsigned int count;
};

struct __attribute__((packed)) nvfs_inode {
    unsigned int size;
    unsigned char type;
    unsigned char reserved[3];
    unsigned int extent_count;
    struct nvfs_extent extents[14];
    unsigned char padding[4];
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

#endif
