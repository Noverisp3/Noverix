#ifndef PAGING_H
#define PAGING_H

#define PAGE_PRESENT 1
#define PAGE_WRITE 2
#define PAGE_USER 4

typedef unsigned int page_dir_entry_t;
typedef unsigned int page_table_entry_t;
typedef page_dir_entry_t *page_dir_t;

extern page_dir_t kernel_page_dir;

void init_paging(unsigned int detected_ram);
page_dir_t page_dir_create(void);
void page_dir_switch(page_dir_t dir);
int map_page_to_dir(page_dir_t dir, unsigned int virt, unsigned int phys, unsigned int flags);
int unmap_page_from_dir(page_dir_t dir, unsigned int virt);
int map_page(unsigned int virt, unsigned int phys, unsigned int flags);
int unmap_page(unsigned int virt);
int get_page_mapping(unsigned int virt, unsigned int *phys_out);
unsigned int read_cr3(void);
unsigned int read_cr0(void);
void dump_page_info(void);
void page_dir_add_user_flag(page_dir_t dir);
void paging_enable_user_access(void);

#endif