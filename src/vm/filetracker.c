#include "vm/filetracker.h"
#include "threads/malloc.h"
#include "threads/synch.h"




struct list fileTrackerList;
struct lock fileTrackerLock;


void filetracker_install(struct file* file_ptr ,uint32_t offset,uint32_t user_vpage,uint32_t pagedir,uint32_t fillBytes);

void
filetracker_init()
{
list_init(&fileTrackerList);
lock_init(&fileTrackerLock);
}



void
filetracker_install(struct file *file_ptr ,uint32_t offset,uint32_t user_vpage,uint32_t pagedir,uint32_t fillBytes)
{

  lock_acquire(&fileTrackerLock);
  struct fileMetadata* MD=(struct fileMetadata*)malloc(sizeof(struct fileMetadata));
  MD->user_vpage=(uint32_t)user_vpage;
  MD->file_ptr=file_ptr;
  MD->file_offset=offset;
  MD->fillBytes=fillBytes;
  MD->pagedir=pagedir;
  list_push_front(&fileTrackerList,&MD->file_elem);
  lock_release(&fileTrackerLock);

}


/*Check whether a given frame is mapped to a file or not
returns NULL if not mapped otherwise returns metadata associated with that file
*/
struct fileMetadata*
filetracker_check(uint32_t user_vpage,uint32_t pagedir)
{
  lock_acquire(&fileTrackerLock);
  struct list_elem* e;
  for (e = list_begin (&fileTrackerList); e != list_end (&fileTrackerList);e = list_next (e))
  {
    struct fileMetadata* MD=list_entry(e,struct fileMetadata,file_elem);
    if(MD->user_vpage==user_vpage && MD->pagedir==pagedir)
    {
    lock_release(&fileTrackerLock);    
    return MD;
    }
  }
  lock_release(&fileTrackerLock);
  return NULL;
  
}









/*Remove file mapping of a frame
*/
void 
filetracker_remove(uint32_t user_vpage,uint32_t pagedir)
{

struct fileMetadata* MD=filetracker_check(user_vpage,pagedir);

lock_acquire(&fileTrackerLock);
if(MD!=NULL)
{
list_remove(&MD->file_elem);
}
lock_release(&fileTrackerLock);

}

/*
Updates the database if finds new filemappings otherwise not
*/

void
filetracker_update(struct file *file_ptr ,uint32_t offset,uint32_t user_vpage,uint32_t pagedir,uint32_t fillBytes)
{

struct fileMetadata*  MD = filetracker_check(user_vpage,pagedir);
if(MD==NULL)
{

filetracker_install(file_ptr ,offset,user_vpage,pagedir,fillBytes);
}

}






