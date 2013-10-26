#include <stddef.h>
#include "lib/string.h"
#include "userprog/pagedir.h"
#include "threads/pte.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "lib/kernel/list.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "vm/filetracker.h"	

void free_struct_raw(struct supp_page* sup_frame);
void *find_sup_page_struct(struct list *sup_pagelist);

void init_sup_page(struct list *sup_pagelist){
	list_init(sup_pagelist);	
	}

// searches for a free slot and returns a address of the slot for table structure
void *find_sup_page_struct(struct list *sup_pagelist){
	//search for free slot in pages
	struct list_elem *e = list_head (sup_pagelist);
      while ((e = list_next (e)) != list_end (sup_pagelist)) 
        {
         	struct supp_page_list *f = list_entry (e, struct supp_page_list, elem);
         	
         	uint32_t i=(uint32_t)f->vpage;
         	for(;i- (uint32_t)f->vpage<PGSIZE-sizeof(struct supp_page);i+=sizeof(struct supp_page)){			//Problem of page overflow
         	
         		struct supp_page *pp=(struct supp_page *) i;
         		if(pp->v_page==0)
         		return pp;
         	}         	         	
        }
       	
	struct supp_page_list * a1=malloc(sizeof (struct supp_page_list));
	a1->vpage=palloc_get_page (0);	
	list_push_back (sup_pagelist, &(a1->elem));
	return a1->vpage;
}

void 
free_struct(uint32_t pd,uint32_t addr)
{

uint32_t *pte=((uint32_t *)pagedir_get_page_raw((uint32_t *)pd,(const void *) addr));
ASSERT(pte!=NULL);
ASSERT(*pte!=0);
ASSERT(((*pte)&PTE_P)!=PTE_P);
struct supp_page * sup_frame= (struct supp_page *)((*pte)&~3);
memset(sup_frame,0,sizeof(struct supp_page));

}

void 
free_struct_raw(struct supp_page* sup_frame)
{
memset(sup_frame,0,sizeof(struct supp_page));
}

//frees the entire supplemental pae structure and also swap space if allocated
void free_supp_page(struct list *sup_pagelist){

	struct list_elem *e = list_head (sup_pagelist);
      while ((e = list_next (e)) != list_end (sup_pagelist))
        {
         	struct supp_page_list *f = list_entry (e, struct supp_page_list, elem);
         	
         	uint32_t i=(uint32_t)f->vpage;
         	for(;i- (uint32_t)f->vpage<PGSIZE-sizeof(struct supp_page);i+=sizeof(struct supp_page)){
         	
         		struct supp_page *pp=(struct supp_page *) i;
         		if(pp->typ==SWAP){
         			free_slot(pp->file_page);// free the swap space
         		}
         		
         	}
         	//free entire page
         	palloc_free_page(f->vpage);         	         	
        }
       	

}






/* Decides whether  for given VA in page dir, pd, frame is in file, swap space, or donot exist*/

enum supplemental_type find_frame_loc( uint32_t* pd,const void *vpage){
	
	uint32_t *pte=((uint32_t *)pagedir_get_page_raw(pd, vpage));
	if(pte==0 ) {
	return NOT_MAPPED;
	}
	if(*pte==0)
	{
	return NOT_MAPPED;	
	}
	if(((*pte) & PTE_P)==PTE_P)
	{
	return NOT_MAPPED;		
	}
	struct supp_page *abc=(struct supp_page *)(*pte & ~0x3);
	return abc->typ;
}

/* In the supplemental pagetable returns th offset*/
off_t get_offset(uint32_t *supp_pd,const void *vpage){

	uint32_t *pte=(uint32_t *)pagedir_get_page_raw(supp_pd, vpage);
	struct supp_page* suppl=(struct supp_page*)(*pte & ~0x3);
	
	ASSERT(((*pte) & PTE_P)==0);
	return suppl->file_page;
}

uint32_t get_read_bytes(uint32_t *supp_pd,const void *vpage){
	uint32_t *pte=(uint32_t *)pagedir_get_page_raw(supp_pd, vpage);	
	ASSERT((*pte & PTE_P)==0);
	uint32_t temp=*pte << 1;
		temp=temp >> 1;
	return(temp >> 3);
}

// true if writable
bool get_writable(uint32_t *supp_pd,const void *vpage){

	uint32_t *pte=(uint32_t *)pagedir_get_page_raw(supp_pd, vpage);
	
	ASSERT(((*pte) & PTE_P)==0);
	return((*pte & PTE_W)==PTE_W);
}

// for populating page directory and supp_pagedirec
bool populate_supp_pgdir(uint32_t* pagedir,const void * vaddr1,off_t offset,
	bool writable,enum supplemental_type typ,uint32_t readdbytes, uint32_t zerobbytes,struct file* file_ptr,struct list * sup_page_dir){
	
	int zerobytes=zerobbytes;
	int readbytes=readdbytes;
	uint8_t * vaddr=(uint8_t *) vaddr1;
	while(readbytes>=PGSIZE||zerobytes>=0){
			struct supp_page *abc= find_sup_page_struct(sup_page_dir);
			
			
			uint32_t *pte2=(uint32_t *)sup_pagedir_set_page(pagedir, vaddr);
			
			*pte2=(uint32_t)abc;
			if(writable){
				*pte2=*pte2|PTE_W;
			}
			force_invalidate_pd(pagedir);

			abc->v_page=(uint32_t) vaddr;
			abc->file_page= offset;
			abc->typ=typ;
		
			if(readbytes>=PGSIZE){
				abc->read_byte=PGSIZE;
				abc->zero_byte=0;
				readbytes-=PGSIZE;	
				if(typ==FILE_SYS)
				filetracker_update(file_ptr,offset,(uint32_t)vaddr,(uint32_t)pagedir,PGSIZE);
			}
			else if((readbytes<PGSIZE) &&(zerobytes>=PGSIZE) ) {    	
				abc->read_byte=readbytes;
				abc->zero_byte=PGSIZE -readbytes;
				//abc->read_byte=readbytes;
				
				if(typ==FILE_SYS && readbytes!=0)
				filetracker_update(file_ptr,offset,(uint32_t)vaddr,(uint32_t)pagedir,readbytes);
				else if(readbytes==0)
				abc->typ=ZEROED_PAGE;
				
				
				zerobytes-=PGSIZE-readbytes;
				readbytes=0;

			}
			else {
				if (zerobytes>0){
					abc->read_byte=readbytes;
					abc->zero_byte=PGSIZE -readbytes;
					zerobytes-=PGSIZE-readbytes;
					if(typ==FILE_SYS)
					filetracker_update(file_ptr,offset,(uint32_t)vaddr,(uint32_t)pagedir,readbytes);
					readbytes=0;
				}
				break;
				
			}
			if(readbytes==0 && zerobytes ==0)
				break;
			
	
			vaddr+=PGSIZE;					
			offset+=PGSIZE;
			
	
		}
	return true;
	
}


/*clears the supp page dir and normal page dir entry in page table */
void clear_before_load(uint32_t* pagedir ,void * vaddr){
	uint32_t *pte=(uint32_t *)pagedir_get_page_raw(pagedir, vaddr);
	ASSERT(pte!=NULL);
	*pte=0x0;
}

/* modifies page table and supplemental page table to add entry for this frame.*/
bool set_supp_pgdir_map(uint32_t* pagedir,void *va,void * frame_vadd){
	
	clear_before_load(pagedir,va);
	//zero ed out page must be writable
	pagedir_set_page (pagedir, va, frame_vadd, true);
	return true;
}
 
