#ifndef FAT16_H
#define FAT16_H

int fat_mount(void);
int fat_list(void);
int fat_read(const char *name, void *buf, unsigned max);
int fat_write(const char *name, const void *data, unsigned size);
int fat_delete(const char *name);

#endif
