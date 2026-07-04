#ifndef PFA_H
#define PFA_H

#define FRAME_SIZE 4096
#define MAX_MEMORY (1024 * 1024 * 1024)    /* 1 GB worst-case */
#define MAX_FRAMES (MAX_MEMORY / FRAME_SIZE)

void pfa_init(unsigned int detected_ram);
void *alloc_frame(void);
void *alloc_frames(unsigned int count);
void free_frame(void *addr);
void free_frames(void *addr, unsigned int count);
int get_free_frame_count(void);

#endif
