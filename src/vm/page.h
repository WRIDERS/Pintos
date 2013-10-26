#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "filesys/off_t.h"
#include "filesys/file.h"
#include "lib/kernel/list.h"

enum supplemental_type {
	NOT_MAPPED=0x0,
	FILE_SYS =0x1,
	SWAP	=0x2,
	ZEROED_PAGE=0x3
};

struct supp_page {
	uint32_t v_page;	// Key
	uint32_t file_page;	//	
	enum supplemental_type typ;	
	uint32_t read_byte;
	uint32_t zero_byte;
};

struct supp_page_list { // will contain list of pages used 
	uint32_t* vpage; // for storing supp_page structures
	struct list_elem elem;
	};
enum supplemental_type find_frame_loc(uint32_t* pd ,const void *vpage);
off_t get_offset(uint32_t *supp_pd,const void *vpage);
uint32_t get_read_bytes(uint32_t *supp_pd,const void *vpage);
bool get_writable(uint32_t *supp_pd,const void *vpage);
bool populate_supp_pgdir(uint32_t* pagedir,const void * vaddr1,off_t offset,bool writable,enum supplemental_type typ,uint32_t readdbytes, uint32_t zerobbytes,struct file* file_ptr,struct list * sup_page_dir);
void clear_before_load(uint32_t* pagedir,void * vaddr);
bool set_supp_pgdir_map(uint32_t* pagedir,void *va,void * frame_vadd);
void free_supp_page(struct list *sup_pagelist);
void init_sup_page(struct list *sup_pagelist);
void  free_struct(uint32_t pd,uint32_t addr);

#endif
