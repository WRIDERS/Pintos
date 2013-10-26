#include "vm/frame.h"
#include "vm/swap.h"
#include "vm/page.h"
#include "vm/filetracker.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "threads/interrupt.h"
#include "userprog/process.h"

static struct  list frame_list;
struct list_elem* clock_hand;


void record_user_page(void* base ,int page_cnt,void* upage);
void delete_user_page(void* base ,int page_cnt);
struct frame* clock_work_algorithm(void);


void 
frame_init()
{
list_init(&frame_list);
lock_init(&frame_lock);
clock_hand=list_head(&frame_list);
}

/*Add entry to list 
*/
void
record_user_page(void* base ,int page_cnt,void* upage)
{

  int i=0;
  for(;i<page_cnt;i++)
  {
    struct frame* tf=malloc(sizeof( struct frame));
    tf->phys_address=(uint32_t)(base+PGSIZE*i);
    tf->page_dir=(uint32_t)thread_current()->pagedir;
    tf->page_user_mapping=(uint32_t)upage;
    tf->sup_page_dir=&(thread_current()->sup_pagelist);
    tf->myExecutable=(struct file*)thread_current()->myExecutable;
    list_push_back(&frame_list,&tf->frame_elem);
  }
}




void 
delete_user_page(void* base ,int page_cnt)
{

  int i=0;

  for(;i<page_cnt;i++)
  {
    struct  list_elem* e;   
    for (e = list_begin (&frame_list); e != list_end (&frame_list);e = list_next (e))
    {
      struct frame *f = list_entry (e, struct frame, frame_elem);
      if(base==(void*)f->phys_address)
      {
        list_remove(e);
        free(f);
        break;
      }
    }
    base+=PGSIZE;
  }


}



void* 
allocate_user_frame(void* upage)
{

  void* allocFrame=NULL;
  
  allocFrame=palloc_get_page(PAL_USER|PAL_ZERO);
  if(allocFrame==NULL)
  {
  allocFrame=evict_user_frame();
  }


  record_user_page(allocFrame,1,upage);
  return allocFrame;
}


/*Deallocation may remove clock hand so 
  we need to update it at every deallocation
*/
void 
updateClockHand(struct list_elem* e)
{
if(e==clock_hand)
{
clock_hand=(list_next(e)==list_end(&frame_list))?list_begin(&frame_list):list_next(e);
}
}

/*
Deallocate the frame if we know the kernel mapping of that page
*/
void
deallocate_user_frame(void* base)
{

  struct  list_elem* e; 
  for (e = list_begin (&frame_list); e != list_end (&frame_list);e = list_next (e))
  {
    struct frame *f = list_entry (e, struct frame, frame_elem);
    if(base==(void*)f->phys_address)
    {
    updateClockHand(e);
      list_remove(e);
       struct fileMetadata* MD=filetracker_check(f->page_user_mapping,f->page_dir);
      if(pagedir_is_dirty((uint32_t *)f->page_dir,(const void *)
      f->page_user_mapping) && pagedir_is_writable((uint32_t *)f->page_dir,(const void *)f->page_user_mapping))
      {
      
      
       if(MD!=NULL)
       {
         dump_one_page(MD->file_ptr,(uint8_t*)f->phys_address,MD->file_offset,MD->fillBytes);
       }
       
      }

      if(MD!=NULL)
      filetracker_remove(f->page_user_mapping,f->page_dir);
      
      free(f);
      palloc_free_page(base);
      return;
    }
  }

  palloc_free_page(base);
  lock_release(&frame_lock);  

}

/*
Use this function only when you are damn sure that you have
created pte for given address in pagedirectory
*/
void
deallocate_user_mapped_frame(uint32_t pagedir,uint32_t uaddr)
{
  struct  list_elem* e; 
  for (e = list_begin (&frame_list); e != list_end (&frame_list);e = list_next (e))
  {
    struct frame *f = list_entry (e, struct frame, frame_elem);
    if(uaddr==f->page_user_mapping && pagedir==f->page_dir)
    {
     updateClockHand(e);
      list_remove(e);
       struct fileMetadata* MD=filetracker_check(f->page_user_mapping,f->page_dir);      
      if(pagedir_is_dirty((uint32_t *)f->page_dir,(const void *)f->page_user_mapping))
      {
       if(MD!=NULL)
       {
       dump_one_page(MD->file_ptr,(uint8_t*)f->phys_address,MD->file_offset,MD->fillBytes);
       }
	if(MD!=NULL)
       filetracker_remove(f->page_user_mapping,f->page_dir);
       
      }
      palloc_free_page((void *)f->phys_address);
      free(f);
      
      clear_before_load((uint32_t *)pagedir,(void *)uaddr);
      
      
      return;
    }
  }
  
  clear_before_load((uint32_t *)pagedir,(void *)uaddr);
  

}

/*
Searches for a frame to evict 
using clock algorithm 
*/

struct frame*
clock_work_algorithm()
{

struct list_elem* e=clock_hand;
struct list_elem* se;
if(e==list_end(&frame_list))
se=list_begin(&frame_list);
else
{
se=list_next(e);
se=(se!=list_end(&frame_list))?se:list_begin(&frame_list);
}

while(se!=e)
{

    struct frame *f = list_entry (se, struct frame, frame_elem);
    uint32_t* pgd=(uint32_t*)f->page_dir;
    void* pti=(void*)f->page_user_mapping;
     
    if(!pagedir_is_accessed(pgd,pti))
    {
    clock_hand=se;    
    return f;
    }
    else
    {
    pagedir_set_accessed(pgd,pti,false);
    } 
       
    se=list_next(se);

    if(se==e) break;

    se=(se!=list_end(&frame_list))?se:list_begin(&frame_list);

}

clock_hand=se;

return NULL;
}


/*After finding a fram to evict we have to write its data either 
  to SWAp or FILESYSTEM
*/
void*
evict_user_frame()
{
  struct frame* f;
  
  while((f=clock_work_algorithm())==NULL);

  
  
  //Check whether frame belongs to filesys or swap and evict accordingly
  struct fileMetadata* MD;
  if((MD=filetracker_check(f->page_user_mapping,f->page_dir))==NULL)
  {
  
  uint32_t swap_no= get_slot();
  bool writable=pagedir_is_writable((uint32_t *)f->page_dir,(const void *)f->page_user_mapping);
  
  enum intr_level prev=intr_disable();
  populate_supp_pgdir((uint32_t *)f->page_dir,(const void *)f->page_user_mapping,swap_no,writable,SWAP,PGSIZE,0,NULL,f->sup_page_dir);
  intr_set_level(prev);
  
  write_swap_sec((void*)f->phys_address,swap_no);
  
  ASSERT(find_frame_loc((uint32_t *)f->page_dir,(const void *)f->page_user_mapping)==SWAP);
  
  }
  else
  {
  
  
  bool datasegment=(MD->file_ptr==NULL);				//dataSegment
  
  if(datasegment)		//Page belongs to data
  {  													//segment should be written to swap	

  /*datasegment=!*/
  uint32_t swap_no =get_slot();
  bool writable=pagedir_is_writable((uint32_t *)f->page_dir,(const void *)f->page_user_mapping);
  filetracker_remove(f->page_user_mapping,f->page_dir);

  enum intr_level prev=intr_disable();
  populate_supp_pgdir((uint32_t *)f->page_dir,(const void *)f->page_user_mapping,swap_no,writable,SWAP,PGSIZE,0,NULL,f->sup_page_dir);
  intr_set_level(prev);
  
  write_swap_sec((void*)f->phys_address,swap_no);
  ASSERT(find_frame_loc((uint32_t *)f->page_dir,(const void *)f->page_user_mapping)==SWAP);

  }
  else
  {

  enum intr_level prev = intr_disable();
  populate_supp_pgdir((uint32_t *)f->page_dir,(const void *)f->page_user_mapping,MD->file_offset,pagedir_is_writable((uint32_t *)f->page_dir,(const void *)f->page_user_mapping),FILE_SYS,PGSIZE,0,MD->file_ptr,f->sup_page_dir);
    intr_set_level(prev);
  
  if(pagedir_is_dirty((uint32_t *)f->page_dir,(const void *)f->page_user_mapping) && pagedir_is_writable((uint32_t *)f->page_dir,(const void *)f->page_user_mapping))
  {
  dump_one_page(MD->file_ptr,(uint8_t*)f->phys_address,MD->file_offset,MD->fillBytes);
  }
  
  }
    
  }

  
  //Gotta invalidate the original page table 
  //and save info in sup_page table
  void* phys_address=(void*)(f->phys_address);
  list_remove(&f->frame_elem);
  free(f);
  
  
  return (void*)phys_address; 

}










