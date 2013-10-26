#ifndef VM_FRAME_H
#define VM_FRAME_H


#include <list.h>
#include <stdio.h>


struct lock frame_lock;


struct frame
{
uint32_t phys_address;
uint32_t page_dir;
struct list* sup_page_dir;
uint32_t page_user_mapping;
struct file* myExecutable;
struct list_elem frame_elem;
};




void  frame_init(void);
void* allocate_user_frame(void* upage);
void deallocate_user_frame(void* base);
void deallocate_user_mapped_frame(uint32_t pagedir,uint32_t uaddr);
void* evict_user_frame(void);
bool routed_load_one_page(uint32_t* pagedir, uint8_t *upage);




#endif
