#ifndef FAT16_H
#define FAT16_H

#define FAT16_EOC 0xFFFF

int fat_mount(void);
int fat_list(const char *path);
int fat_read(const char *name, void *buf, unsigned max);
int fat_write(const char *name, const void *data, unsigned size);
int fat_delete(const char *name);
int fat_mkdir(const char *path);
int fat_rmdir(const char *path);
int fat_chdir(const char *path, unsigned *out_cluster);

#endif
