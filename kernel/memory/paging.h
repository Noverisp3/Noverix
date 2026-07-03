#ifndef PAGING_H
#define PAGING_H

#define PAGE_PRESENT 1
#define PAGE_WRITE 2
#define PAGE_USER 4

void init_paging(void);
int map_page(unsigned int virt, unsigned int phys, unsigned int flags);
int unmap_page(unsigned int virt);
int get_page_mapping(unsigned int virt, unsigned int *phys_out);
unsigned int read_cr3(void);
unsigned int read_cr0(void);
void dump_page_info(void);

#endif
