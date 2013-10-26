#ifndef FILESYS_BCACHE_H
#define FILESYS_BCACHE_H

#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"


#define B_VALID 0x1
#define B_DIRTY 0x2
#define B_BUSY 0x4

#define B_CLOCK_MASK (~(B_VALID|B_DIRTY))
#define B_ACCESS_MASK  0x38 //(8+16+32)
#define B_ACCESS_INC 0x8


#define MAX_CACHE_SIZE 64		//Maximum No of Disk blocks allowed to be  kept in memory


struct bufferelem {

struct  list_elem blink;
block_sector_t sector_no;

char cached_data[BLOCK_SECTOR_SIZE];

uint32_t bflags;

struct semaphore bqueue;

};

struct read_ahead_elem
{
  struct list_elem rlink;
  block_sector_t sector;
};



void bcache_init(struct block * fs_device);
void bcache_read(block_sector_t sector , void * mem_buf);
void bcache_write(block_sector_t sector , const void * mem_buf);
void bcache_flush(bool forced);
struct bufferelem * bcache_get_ref(block_sector_t sector);
void bcache_drop_ref (struct bufferelem* buf,bool is_Dirty);





#endif


