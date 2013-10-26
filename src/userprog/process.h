#ifndef USERPROG_PROCESS_H
//11360
#define USERPROG_PROCESS_H
#include "filesys/off_t.h"
#include "threads/thread.h"
#include <inttypes.h>

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
bool load_one_page(uint32_t* pagedir, uint8_t *upage);
struct file;
bool dump_one_page(struct file* file_ptr, uint8_t *kpage,off_t offset,uint32_t fillBytes);
#endif
