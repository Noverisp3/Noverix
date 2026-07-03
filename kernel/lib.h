#ifndef LIB_H
#define LIB_H

void lib_memcpy(void *dst, const void *src, unsigned int n);
void lib_memset(void *dst, int v, unsigned int n);
int lib_strlen(const char *s);
int lib_strcmp(const char *a, const char *b);
void lib_strcpy(char *dst, const char *src);

#endif
