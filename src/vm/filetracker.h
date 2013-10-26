#ifndef VM_FILETRACKER_H
#define VM_FILETRACKER_H

#include <list.h>
#include <string.h>





struct fileMetadata
{
uint32_t user_vpage;
struct file* file_ptr;
uint32_t  file_offset;
uint32_t  fillBytes;
uint32_t  pagedir;
struct list_elem file_elem;
};


struct fileMetadata* filetracker_check(uint32_t user_vpage,uint32_t pagedir);
void filetracker_init(void);
void filetracker_update(struct file *file_ptr ,uint32_t offset,uint32_t user_vpage,uint32_t pagedir,uint32_t fillBytes);
void filetracker_remove(uint32_t user_vpage,uint32_t pagedir);












#endif
