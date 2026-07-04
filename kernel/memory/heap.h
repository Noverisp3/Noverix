#ifndef HEAP_H
#define HEAP_H

#define HEAP_START 0x00800000
#define HEAP_SIZE  0x00200000

void heap_init(void);

/* Thread-safe public API (acquires heap_lock internally) */
void *malloc(unsigned int size);
void *realloc(void *ptr, unsigned int size);
void *calloc(unsigned int num, unsigned int size);
void free(void *ptr);
void heap_walk(void);

/* Ring-3-safe wrappers (no cli) */
void *malloc_user(unsigned int size);
void *realloc_user(void *ptr, unsigned int size);
void free_user(void *ptr);

#endif
