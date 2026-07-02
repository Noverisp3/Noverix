#ifndef PFA_H
#define PFA_H

#define FRAME_SIZE 4096
#define MAX_MEMORY (32 * 1024 * 1024)
#define MAX_FRAMES (MAX_MEMORY / FRAME_SIZE)

void pfa_init(void);
void *alloc_frame(void);
void free_frame(void *addr);

#endif
