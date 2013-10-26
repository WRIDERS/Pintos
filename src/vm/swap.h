#ifndef VM_SWAP_H

#define VM_SWAP_H

#include <stddef.h>
#include <inttypes.h>
#include "threads/vaddr.h"
#include "devices/block.h"

#define SWAP_ATOMICITY PGSIZE/BLOCK_SECTOR_SIZE

void swap_init(void);
uint32_t write_swap(const void *buffer);
void read_swap_sec(void *buffer,uint32_t sec);
void free_slot(uint32_t slot);
uint32_t get_slot(void);
void write_swap_sec(const void *buffer,block_sector_t sec);
#endif
