#ifndef HEAP_H
#define HEAP_H

#define HEAP_START 0x00800000
#define HEAP_SIZE  0x00200000

void heap_init(void);
void *malloc(unsigned int size);
void free(void *ptr);

#endif
