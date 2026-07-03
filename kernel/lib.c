#include "lib.h"

void lib_memcpy(void *dst, const void *src, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
}

void lib_memset(void *dst, int v, unsigned int n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)v;
}

int lib_strlen(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

int lib_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

void lib_strcpy(char *dst, const char *src)
{
    if (!dst || !src) return;
    while ((*dst++ = *src++));
}
