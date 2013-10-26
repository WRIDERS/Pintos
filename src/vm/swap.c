#include <stddef.h>
#include <bitmap.h>
#include <debug.h>
#include <stdio.h>
#include "devices/block.h"
#include "vm/swap.h"
#include "threads/synch.h"
#include "threads/thread.h"


struct bitmap* swap_table;
struct block*  sw_device;
struct lock swap_lock;

block_sector_t get_slot(void);
void write_swap_sec(const void *buffer,block_sector_t sec);
void read_swap_sec(void *buffer,uint32_t sec);

uint32_t SIZE_SWAP_PAGE =0;// 1024 pages= 4mb


void 
swap_init()
{
  sw_device=block_get_role(BLOCK_SWAP);
  SIZE_SWAP_PAGE=(block_size(sw_device));
  SIZE_SWAP_PAGE/=8;
  swap_table=bitmap_create(SIZE_SWAP_PAGE);
  lock_init(&swap_lock);
  ASSERT(sw_device!=NULL);
}



/* Get the sector number
*/
uint32_t
get_slot()
{
	lock_acquire(&swap_lock);
	uint32_t slot =bitmap_scan_and_flip(swap_table,0,1,false);
	lock_release(&swap_lock);
	return slot;
}

/*Write  one page from buffer to page in swap startng from sector 
sec buffer must have a full one page data.
*/ 
void 
write_swap_sec(const void *buffer,block_sector_t sec)
{
        ASSERT (sw_device!=NULL);
	
	ASSERT(bitmap_test(swap_table,sec));	
	lock_acquire(&swap_lock);	
	int i=0;
	for(;i<SWAP_ATOMICITY;i++)
		block_write (sw_device,sec*SWAP_ATOMICITY+i, buffer+BLOCK_SECTOR_SIZE*i);
	lock_release(&swap_lock);
}

/*reads from swap to buffer
*/
void 
read_swap_sec(void *buffer,uint32_t sec)
{
	
	ASSERT(sec<SIZE_SWAP_PAGE);
	ASSERT(bitmap_test(swap_table,sec));
	
	lock_acquire(&swap_lock);
	int i=0;
	for(;i<SWAP_ATOMICITY;i++)
		block_read (sw_device, sec*SWAP_ATOMICITY+i,buffer+BLOCK_SECTOR_SIZE*i);

        bitmap_flip(swap_table,(size_t)sec);
	lock_release(&swap_lock);        
        ASSERT (sw_device!=NULL);
        
}

/* Writes one page to swap space and returns page number
*/
uint32_t 
write_swap(const void *buffer)
{	
	block_sector_t sector= get_slot();
	write_swap_sec(buffer, sector);
	return sector;
}

/*Cleanup function called by a dying process to free swap space
*/
void 
free_slot(uint32_t slot)
{

ASSERT(bitmap_test(swap_table,slot));

lock_acquire(&swap_lock);
bitmap_set(swap_table,slot,false);
lock_release(&swap_lock);

}



