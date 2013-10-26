#include "filesys/bcache.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "devices/timer.h"
#include <stdio.h>
#include <string.h>

struct list bcache_list;
struct list read_ahead_list;

struct lock bcache_sync_lock;
struct lock read_ahead_lock;
struct semaphore read_ahead_queue;

struct block * filesys_token;

/*
Initializes the list of bcache elements 
*/
struct bufferelem* bcache_search(block_sector_t sector);

void buffer_flush_daemon(void*);
void dump_buf(struct bufferelem* buf);

void decrement_access(uint32_t* flag);
void increment_access(uint32_t* flag);
bool is_evictable(uint32_t *flag);
bool check_flag(uint32_t Flags , uint32_t bit_flag );


void read_ahead_daemon (void* aux);
void put_read_ahead_request(block_sector_t sector);


bool killDaemon=false;
struct semaphore killedDaemon;


bool killDaemon2=false;
struct semaphore killedDaemon2;


void 
bcache_init(struct block * fs_device)
{
  filesys_token=fs_device;

  list_init(&bcache_list);
  list_init(&read_ahead_list);


  lock_init(&bcache_sync_lock);
  lock_init(&read_ahead_lock);
  sema_init(&read_ahead_queue,0);
  
  sema_init(&killedDaemon,0);
  sema_init(&killedDaemon2,0);

  struct bufferelem* buf;
  int i;
  for(i=0;i<MAX_CACHE_SIZE;i++)
    {
      buf=(struct bufferelem*)malloc(sizeof(struct bufferelem));
      sema_init(&buf->bqueue,1);
      buf->bflags=0x0;
      buf->sector_no=0;
      list_push_front(&bcache_list,&buf->blink);
    }
//    printf("BCACHE INIT %p\n",thread_current());
  char NAME1[16]="FLUSH_DAEMON";
  thread_create(NAME1,PRI_DEFAULT,buffer_flush_daemon,NULL);

  char NAME2[16]="READ_DAEMON";
  thread_create(NAME2,PRI_DEFAULT,read_ahead_daemon,NULL);


}


struct bufferelem*
bcache_search(block_sector_t sector)
{
  struct list_elem * e;
//  printf("SEARCH\n");
  scan:
  lock_acquire(&bcache_sync_lock);
//  if(sector==202)printf("Lock acquire by %x\n",thread_current());
  for(e=list_begin(&bcache_list);e!=list_end(&bcache_list);e=list_next(e))
    {
      struct bufferelem* belem = list_entry(e,struct bufferelem,blink);
      
      if(belem->sector_no==sector && check_flag(belem->bflags , B_VALID))
        {
          if(check_flag( belem->bflags , B_BUSY))
      	    {
//      	      if(sector==202)printf("Gonna Wait for treasure by %x\n",thread_current());
      	      lock_release(&bcache_sync_lock);	//Release the lock and sleep on the buffer 
//      	      printf("Busy %x \n",thread_current());
//      	      printf("Never Up\n");
//	      printf("BUSY\t");
      	      sema_down(&belem->bqueue);
//      	      printf("BEE\n");
      	      sema_up(&belem->bqueue);
      	      goto scan;
      	    }
      	  else
      	    {
      	      if(!sema_try_down(&belem->bqueue))
      	       PANIC("Brutal Race Condition \n");
      	      increment_access(&belem->bflags);
      	      ASSERT(!check_flag(belem->bflags , B_BUSY));
      	      belem->bflags|=B_BUSY;
      	      lock_release(&bcache_sync_lock);		//Release the lock after setting it busy so that others will sleep on it
      	      						//Right now I am blocking whether it is read or write .Later will implement multiple reads 
      	      						//in parallel
      	      return belem;
      	      //do stuff
      	    }
        }
    }
    
//   printf("Below wait\n");

	/*
	The Fact that I am here means that buffer cache has failed me .
	So I have deal with it the hard way and throw someone back to dull life of filesys
	*/



  struct bufferelem* to_evict=NULL;

  ///Iterate over buffer list and choose a block to evict
  //Right now lazy implementation just evict the which is not busy
  struct list_elem* e1;
  
  while(to_evict==NULL){
//  printf("Zoom into Clock\n");
  for(e1=list_begin(&bcache_list);e1!=list_end(&bcache_list);e1=list_next(e1))
    {
      struct bufferelem* buf=list_entry(e1,struct bufferelem,blink);
      if(is_evictable(&buf->bflags))
        {
          to_evict=buf;
          break;
        }
        else
        decrement_access(&buf->bflags);
    }
    }

/*  
  for(e1=list_begin(&bcache_list);e1!=list_end(&bcache_list);e1=list_next(e1))
    {
      struct bufferelem* buf=list_entry(e1,struct bufferelem,blink);
      if(!check_flag(buf->bflags , B_VALID))
        { 
          to_evict=buf;
          break;
        }
    }
    
  int cont=0;
  while(to_evict==NULL){
  ASSERT(cont==0 || cont==MAX_CACHE_SIZE);
  if(cont>0) printf("\t\tBusy BEE\n");
  cont=0;
  for(e1=list_begin(&bcache_list);e1!=list_end(&bcache_list);e1=list_next(e1))
    {
      struct bufferelem* buf=list_entry(e1,struct bufferelem,blink);
      if(!check_flag(buf->bflags , B_BUSY))
        {
          to_evict=buf;
          break;
        }
       cont++;
    }
//    if(!to_evict) printf("BUSY BEES %d\n",cont);
  }
*/  


//  if(MEGABUSY) printf("Escaped MEGABUSY\n");

  ASSERT(to_evict!=NULL);

//  printf("Eviction at its best  %x\n",to_evict);
  block_sector_t flush_sector=to_evict->sector_no;
  uint32_t flush_flags=to_evict->bflags;
  to_evict->sector_no = sector;
//  to_evict->bflags=0x0;    	//Clear all the flags so that no one will wait for this block
  to_evict->bflags=(B_BUSY|B_VALID);	//Set it busy so that no one will see it again
  increment_access(&to_evict->bflags);
  /*
  If buffer is not busy then sema_down will not block 
  */
  if(!sema_try_down(&to_evict->bqueue))		//Any waiters will wait
    PANIC("Brutal Race Condition \n");
//    if(sector==202){ printf("Bringing it back by %x  \n",thread_current());
 //                 printf("Lock Realease by %x\n",thread_current());    }
 // if(flush_sector==202) printf("Throwing my block by %x  and flags =%x  \n",thread_current(),flush_flags);
  lock_release(&bcache_sync_lock);


  if(check_flag(flush_flags , B_BUSY))		//Will try to prevent this as much as possible 
    {						//For this case need to use a different flag named evicting so that 2 evictions cannot run in 
						//parallel
//      sema_down();
      PANIC("The prodigal son returns\n");
      return to_evict;
    }
  else if(check_flag(flush_flags , B_VALID) && check_flag(flush_flags , B_DIRTY))
    {
//      printf("Something is dirty\n");
      block_write(filesys_token,flush_sector,to_evict->cached_data);					//do block write 
      block_read(filesys_token,sector,to_evict->cached_data);
      return to_evict;
    }
  else
    {
      block_read(filesys_token,sector,to_evict->cached_data);					//do block write 
      return to_evict;    
    }
    
    
  PANIC("SHOULDN'T BE HERE\n");    
  return NULL;
}

/*
This is substitute for block_read()
*/
void 
bcache_read(block_sector_t sector , void * mem_buf)
{
    ASSERT(sector<block_size(filesys_token));

    struct bufferelem* buf=bcache_search(sector);
    ASSERT(buf!=NULL);
    
    memcpy(mem_buf,buf->cached_data,BLOCK_SECTOR_SIZE);
    
    /*
    Here lock is acquired to maintain the invariant that if buffer is not busy then sema_down will not block
    */
    
//    lock_acquire(&bcache_sync_lock);
    intr_disable();
    buf->bflags &=~B_BUSY;      	            	      //Tricky
    sema_up(&buf->bqueue);
    intr_enable();

    put_read_ahead_request(sector+1);
//    lock_release(&bcache_sync_lock);
}

/*
This is substitute for block_write()
*/
void 
bcache_write(block_sector_t sector , const void * mem_buf)
{   
//    printf("BCACHE WRITE TO SECTOR %d \n",sector);
  ASSERT(sector<block_size(filesys_token));

    struct bufferelem* buf=bcache_search(sector);
    ASSERT(buf!=NULL);
    
    memcpy(buf->cached_data,mem_buf,BLOCK_SECTOR_SIZE);

    /*
    Here lock is acquired to maintain the invariant that if buffer is not busy then sema_down will not block
    */
    
//    lock_acquire(&bcache_sync_lock);

    intr_disable();
    buf->bflags|=B_DIRTY;
    buf->bflags &=~B_BUSY;      	            	      //Tricky
    sema_up(&buf->bqueue);
    intr_enable();

    put_read_ahead_request(sector+1);

//    lock_release(&bcache_sync_lock);
//    printf("Successfully written %d\n",sector);
}

/*
Flushes the buffer cache to filesys 
If forced is true then waits for BUSY transactions and flushes them to disk
*/
void 
bcache_flush(bool forced)
{

    if(forced)
    {
//      printf("Flush Forced\n");
//      ASSERT(list_empty(&bcache_list));
      killDaemon2=true;      
      killDaemon=true;
      sema_up(&read_ahead_queue);
      sema_down(&killedDaemon2);
      sema_down(&killedDaemon);
      
//      printf("I am outta here \n");
    }
  lock_acquire(&bcache_sync_lock);
//  printf("FLUSH CYCLE\n");
  struct list_elem * e;
  for(e=list_begin(&bcache_list);e!=list_end(&bcache_list);)
    {
      struct bufferelem* buf =list_entry(e,struct bufferelem,blink);
       
            
      if((buf->bflags & B_VALID))
        {
          if(forced) {ASSERT(!check_flag(buf->bflags , B_BUSY))};
          
          if((!check_flag(buf->bflags , B_BUSY)) && check_flag(buf->bflags , B_DIRTY))
            {
//              if(!sema_try_down(&buf->bqueue))		//Any waiters will wait
//                PANIC("Brutal Race Condition \n");
              block_write(filesys_token,buf->sector_no,buf->cached_data);
              buf->bflags&=~B_DIRTY;
//             sema_up(&buf->bqueue);
            }
        }
        if(forced) list_remove(e);
        e=list_next(e);
        if(forced) free(buf);
    }
//    printf("FLUSH CYCLE END\n");    
    lock_release(&bcache_sync_lock);
    
}

/*
Gives a reference to Disk Data Block present in memory .
*/
struct bufferelem * 
bcache_get_ref(block_sector_t sector)
  {
  ASSERT(sector<=block_size(filesys_token));
    struct bufferelem* buf=bcache_search(sector); 
//    put_read_ahead_request(sector+1);
    return buf;
  }



void 
bcache_drop_ref (struct bufferelem* buf,bool is_Dirty)
  {
//    lock_acquire(&bcache_sync_lock);
    intr_disable();
    if(is_Dirty)
      buf->bflags |= B_DIRTY;
    buf->bflags &= (~B_BUSY);
    sema_up(&buf->bqueue);
    intr_enable();
//    lock_release(&bcache_sync_lock);
  }

/*
Dumps the bufferelem on stdio .
Used for debufging purposes .
*/
void 
dump_buf(struct bufferelem* buf)
  {
    printf("Buffer address = %p\n",buf);
    printf("Buffer flags %x \n",buf->bflags);
  }

void 
put_read_ahead_request(block_sector_t sector)
  {
    if(sector >= block_size(filesys_token))
    return;

    struct read_ahead_elem* rh=(struct read_ahead_elem*)malloc(sizeof(struct read_ahead_elem));
//    printf("CQUIRE LOCK\n");
//    lock_acquire(&read_ahead_lock);
//    printf("CQUIRED\n");
    rh->sector=sector;
    intr_disable();
    list_push_back(&read_ahead_list,&rh->rlink);
    intr_enable();
    sema_up(&read_ahead_queue);
//    lock_release(&read_ahead_lock);
//    printf("RELEASE\n");
  }


void
read_ahead_daemon (void* aux)
  {
  
    while(true)
      {
//        printf("WHILE\t");
        sema_down(&read_ahead_queue);
        if(killDaemon2) break;
//        lock_acquire(&read_ahead_lock);
//        printf("READ AHEAD\t");
        intr_disable();
        if(!list_empty(&read_ahead_list))
        {
//          printf("POP \t");
          struct list_elem* e=list_pop_front(&read_ahead_list);
          intr_enable();
          ASSERT(e!=NULL);
//          lock_release(&read_ahead_lock);
//          printf("RELEASE %x \t",thread_current());
          struct read_ahead_elem* rhl=list_entry(e,struct read_ahead_elem,rlink);
          struct bufferelem* buf = bcache_search(rhl->sector);
//          printf("SEARCH COMPLETE\n");
          intr_disable();
          buf->bflags&=~B_BUSY;
          sema_up(&buf->bqueue);
          intr_enable();
        }
        else
        PANIC("SOMETHING AINT RIGHT\n");

      }
      
      sema_up(&killedDaemon2);
      thread_exit();
  }




void 
buffer_flush_daemon(void * aux)
  {
//    printf("GotHere %x \n",thread_current() );
    while(true)
      {
        timer_msleep(1000);
//        printf("Woke UP %d\n",killDaemon);
        if(killDaemon)
          break;
        bcache_flush(false);
      }
      sema_up(&killedDaemon);
//      printf("Killed Daemon %d \n",killedDaemon);      
      thread_exit();
  }


void
increment_access(uint32_t* flag)
  {
    uint32_t tempfl=*flag;
    tempfl=(tempfl & B_ACCESS_MASK)==B_ACCESS_MASK?tempfl:(tempfl+B_ACCESS_INC);
    *flag=tempfl;
  }

void 
decrement_access(uint32_t* flag)
  {
    uint32_t tempfl=*flag;
    tempfl=(tempfl & B_ACCESS_MASK)==0?tempfl:(tempfl-B_ACCESS_INC);
    *flag=tempfl;
  }

bool 
is_evictable(uint32_t *flag)
{
  uint32_t tempfl=*flag;

  return ((tempfl & B_CLOCK_MASK)==0);

}

bool
check_flag(uint32_t Flags , uint32_t bit_flag )
  {
//  printf("Flag =%x \n",Flags);
    uint32_t temp=(Flags & bit_flag);
    
    return temp==bit_flag;
  }







