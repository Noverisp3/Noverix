#ifndef PAGING_H
#define PAGING_H

#define PAGE_PRESENT 1
#define PAGE_WRITE 2
#define PAGE_USER 4

void init_paging(void);
int map_page(unsigned int virt, unsigned int phys, unsigned int flags);
unsigned int read_cr3(void);
unsigned int read_cr0(void);

#endif
