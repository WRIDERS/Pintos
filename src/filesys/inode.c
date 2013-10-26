#include "filesys/inode.h"
//6616
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/bcache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define LINKED_ADDRESS 1
#define MAX_ADDRESS 127
#define MAX_ADDRESS_MAIN 125



struct lock inode_sync_lock;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */

struct inode_disk
  {
//    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t direct_address[MAX_ADDRESS_MAIN];               /* Not used. */
    uint32_t indirect_address;
  };


/* An inode dedicated just for storing address and making our lives easier 
*/
struct address_dedicated_inode_disk
  {
    uint32_t direct_address[MAX_ADDRESS];
    uint32_t indirect_address;
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 						//This inode should have enough information to reload disk_inode when demanded from buffer
							// cache
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
//    struct inode_disk data;             /* Inode content. */
    bool isDirectory;
    struct inode_disk * data;
    struct bufferelem* buf;
    struct lock inode_xtension_lock;    
  };

void reload_inode(struct inode* id);
void done_inode(struct inode* id,bool dirty);

void
reload_inode(struct inode* id)
{
  struct bufferelem* buf=bcache_get_ref(id->sector);
  id->buf=buf;
  id->data=(struct inode_disk*)buf->cached_data;
}

void
done_inode(struct inode* id,bool dirty)
{
  struct bufferelem* tbuf=id->buf;
  id->buf=NULL;
  id->data=NULL;
  bcache_drop_ref(tbuf,dirty);

}


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector ( struct inode *inode, off_t pos )
{

  reload_inode(inode);

  ASSERT (inode != NULL);
 
  uint32_t temp;
/*  if (pos < (inode->data)->length)
    temp = (inode->data)->start + pos / BLOCK_SECTOR_SIZE;
  else
    temp = -1;
*/
//    printf("READ INODE  POS %d\n",pos);

    if(pos<(BLOCK_SECTOR_SIZE*MAX_ADDRESS_MAIN))
    {
//    temp=(inode->data)->direct_address[(pos/BLOCK_SECTOR_SIZE)-((pos%BLOCK_SECTOR_SIZE)==0 && pos!=0)];
    temp=(inode->data)->direct_address[(pos/BLOCK_SECTOR_SIZE)];
    
    
//    printf("Decent \t");
    }
    else
    {
    
    uint32_t seek_bytes=pos-(BLOCK_SECTOR_SIZE*MAX_ADDRESS_MAIN);
    uint32_t NIndirect=(seek_bytes/(BLOCK_SECTOR_SIZE*MAX_ADDRESS));//-((seek_bytes%(BLOCK_SECTOR_SIZE*MAX_ADDRESS))==0 && seek_bytes!=0);
    uint32_t left_bytes=seek_bytes-(NIndirect*(BLOCK_SECTOR_SIZE*MAX_ADDRESS));
    uint32_t BIndex=((left_bytes)/BLOCK_SECTOR_SIZE);//-((left_bytes%BLOCK_SECTOR_SIZE)==0 && left_bytes!=0);
//    printf("BIndex %d NIndirect %d\t",BIndex,NIndirect);
    uint32_t i=0;
    uint32_t next_sector=(inode->data)->indirect_address;
    struct bufferelem* buff;
    struct address_dedicated_inode_disk* adinode;
   
    for(i=0;i<=NIndirect;i++)
      {
        buff=bcache_get_ref(next_sector);
        adinode=(struct address_dedicated_inode_disk*)buff->cached_data;
//        int j=0;
//        if(NIndirect==1){
//        for(j=0;j<MAX_ADDRESS;j++)
//        printf("SEC %d = %d\n",j,adinode->direct_address[j]);
//        }
        if(i==NIndirect)
        break;
        
        next_sector=adinode->indirect_address;        
        bcache_drop_ref(buff,false);
//        printf("Loop next %d\t",next_sector);
      }
      temp=adinode->direct_address[BIndex];
      bcache_drop_ref(buff,false);

    }
    

    done_inode(inode,false);
    if(temp>=4096) printf("SECTOR=%d pos %d\n",temp,pos);
    
    return temp;

}

void inode_update_extend(struct inode * inode,off_t pos,off_t size);
bool inode_check_read_bound (struct inode* inode ,off_t pos ,off_t* size);
bool inode_extend (struct inode * inode,off_t pos,off_t size);

bool 
inode_check_read_bound (struct inode* inode ,off_t pos ,off_t* size)
  {
    lock_acquire(&inode->inode_xtension_lock);
    reload_inode(inode);
    off_t sz=*size;
    bool validity=false;
    
    if((sz+pos)<=(inode->data)->length)
    validity=true;
    else
    *size=((inode->data)->length)-pos;
    
    done_inode(inode,false);
    lock_release(&inode->inode_xtension_lock);
    return validity;
  }

bool
inode_extend (struct inode * inode,off_t pos,off_t size)
  {
//  printf("EXTEND\n");
        lock_acquire(&inode->inode_xtension_lock);
        reload_inode(inode);
    
        struct inode_disk* disk_inode=inode->data;
        size_t sectors = bytes_to_sectors (pos+size);
//        disk_inode->length = length;
//        disk_inode->magic = INODE_MAGIC;
	bool xtend=false;
        bool disk_full=false;
	
        uint32_t sectors_left = (uint32_t)sectors;
        int i=0;
        for(i=0;i<MAX_ADDRESS_MAIN;i++)
        {
          if(sectors_left<=0) break;
          if((disk_inode->direct_address[i]==NULL))
          disk_full=!free_map_allocate(1,(disk_inode->direct_address+i));
          if(disk_full) {
            disk_inode->direct_address[i]=NULL;
            sectors_left=0;            
          }
          sectors_left--;
        }
//        printf("CREATED %d\n",sectors_left);
//        for(i=0;i<MAX_ADDRESS_MAIN;i++)
//        {
//        printf("SEC %d = %d\n",i,disk_inode->direct_address[i]);
 //         if(sectors_left==0) break;          
//          free_map_allocate(1,(disk_inode->direct_address+i));
//          sectors_left--;
//        }

        struct address_dedicated_inode_disk* ded_inode=NULL;
        uint32_t newSector=0;
	struct bufferelem* buff=NULL;
	bool bufferAlloc=false;
        if(sectors_left>0)
        {
        if(xtend=(disk_inode->indirect_address==NULL))
        {
        disk_full=!free_map_allocate(1,&newSector);
        disk_inode->indirect_address=newSector;
        if(disk_full){
        disk_inode->indirect_address=NULL;
        sectors_left=0;
        }
        bufferAlloc=true;
        ded_inode=calloc(1,sizeof(struct address_dedicated_inode_disk));
	}
	else
	{
//	buff=bcache_get_ref(disk_inode->indirect_address);
//	ded_inode=(struct address_dedicated_inode_disk*)buff->cached_data;
	newSector=disk_inode->indirect_address;
	}
        }

//        bcache_write(sector,disk_inode);
        
 	while(sectors_left>0)
 	{
 //       printf("Linked Inode %d\n",newSector);
 	if(xtend)
 	{
 	if(!bufferAlloc) ded_inode=calloc(1,sizeof(struct address_dedicated_inode_disk));
 	memset(ded_inode,0,sizeof(struct address_dedicated_inode_disk));
 	}
	else
	{
	buff=bcache_get_ref(newSector);
	ded_inode=(struct address_dedicated_inode_disk*)buff->cached_data;	
	}

 	int i=0;
 	for(i=0;i<MAX_ADDRESS;i++,sectors_left--)
 	{
   	  if(sectors_left<=0) break;
   	  if((ded_inode->direct_address[i]==NULL))
 	  disk_full=!free_map_allocate(1,(ded_inode->direct_address+i));
 	  if(disk_full){
            disk_inode->direct_address[i]=NULL;
            sectors_left=0;
 	  }
 	  
 	}

// 	for(i=0;i<MAX_ADDRESS;i++)
// 	{
//   	  if(sectors_left==0) break;
// 	  free_map_allocate(1,(ded_inode->direct_address+i)); 	  
 //          printf("SEC %d = %d\n",i,ded_inode->direct_address[i]);
// 	}
 	
 	bool tempxtend=xtend; 	
 	uint32_t flush_sector=newSector;
 	if(sectors_left>0)
 	{
 	  if(xtend=((ded_inode->indirect_address)==NULL))
 	  { 
   	    disk_full=!free_map_allocate(1,&newSector);   
            ded_inode->indirect_address=newSector;
            if(disk_full){
            ded_inode->indirect_address=NULL;            
            sectors_left=0;
            }
          }
          else
          newSector=ded_inode->indirect_address;
 	}
 	

 	if(!tempxtend)
 	  bcache_drop_ref(buff,true); 	  
	else
   	  bcache_write(flush_sector,ded_inode);
 	
 	}

    
    if(bufferAlloc) free(ded_inode);
        
    
    done_inode(inode,true);
    return !disk_full;
  }

void
inode_update_extend(struct inode * inode,off_t pos,off_t size)
{
    reload_inode(inode);
//    printf("SETTING FILESIZE=%d\n",pos+size);
    if(size>0)
    (inode->data)->length=(size+pos);

    done_inode(inode,true);
    lock_release(&inode->inode_xtension_lock);
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init(&inode_sync_lock);
}

void
inode_set_free_map(block_sector_t alloc_sector,uint32_t sect_cnt,off_t byte_size)
  {
    struct inode_disk* free_map_inode=(struct inode_disk*)malloc(sizeof(struct inode_disk));
    memset(free_map_inode,0,sizeof(struct inode_disk));
    bcache_read(FREE_MAP_SECTOR,free_map_inode);
    ASSERT(free_map_inode->length==0);
    int i=0;
    for(i=0;i<sect_cnt;i++)
      {
        free_map_inode->direct_address[i]=(alloc_sector+i);
      }
      free_map_inode->length=byte_size;
      bcache_write(FREE_MAP_SECTOR,free_map_inode);      
      free(free_map_inode);
  }



/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;
//  printf("\nHERE\n");
  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  
  disk_inode = calloc (1, sizeof *disk_inode);
//  printf("CREATE INODE  at %d size %d\n",sector,length);
  if (disk_inode != NULL)
    {
        size_t sectors = bytes_to_sectors (length);
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;
//        printf("CREATING %d\n",sectors);
//      if (free_map_allocate (sectors, &disk_inode->start)) 
//        {

        uint32_t sectors_left = (uint32_t)sectors;
        int i=0;
        for(i=0;i<MAX_ADDRESS_MAIN;i++)
        {
          if(sectors_left==0) break;          
          free_map_allocate(1,(disk_inode->direct_address+i));
          sectors_left--;
        }
//        printf("CREATED %d\n",sectors_left);
//        for(i=0;i<MAX_ADDRESS_MAIN;i++)
//        {
//        printf("SEC %d = %d\n",i,disk_inode->direct_address[i]);
 //         if(sectors_left==0) break;          
//          free_map_allocate(1,(disk_inode->direct_address+i));
//          sectors_left--;
//        }

        struct address_dedicated_inode_disk* ded_inode=NULL;

        uint32_t newSector=0;

        if(sectors_left>0)
        {
        free_map_allocate(1,&newSector);
        disk_inode->indirect_address=newSector;
        ded_inode=calloc(1,sizeof(struct address_dedicated_inode_disk));
        }
        bcache_write(sector,disk_inode);
        
 	while(sectors_left>0)
 	{
 //       printf("Linked Inode %d\n",newSector);
 	memset(ded_inode,0,sizeof(struct address_dedicated_inode_disk));
 	int i=0;
 	for(i=0;i<MAX_ADDRESS;i++,sectors_left--)
 	{
   	  if(sectors_left==0) break;
 	  free_map_allocate(1,(ded_inode->direct_address+i)); 	  
 	}

 	for(i=0;i<MAX_ADDRESS;i++)
 	{
//   	  if(sectors_left==0) break;
// 	  free_map_allocate(1,(ded_inode->direct_address+i)); 	  
 //          printf("SEC %d = %d\n",i,ded_inode->direct_address[i]);
 	}

 	uint32_t flush_sector=newSector;
 	if(sectors_left>0)
 	{
 	  free_map_allocate(1,&newSector);
          ded_inode->indirect_address=newSector;
 	}
 	bcache_write(flush_sector,ded_inode); 	  
 	
 	}
 		
 	success=true;

/*
          bcache_write(sector, disk_inode);

          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                bcache_write(disk_inode->start + i, zeros);
                //block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
*/
//        }

      free (disk_inode);
      if(ded_inode!=NULL)
      free(ded_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;
  lock_acquire(&inode_sync_lock);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
//          inode_reopen (inode);
	  inode->open_cnt++;
          lock_release(&inode_sync_lock);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->buf=NULL;
  inode->data=NULL;
  inode->removed = false;
  inode->isDirectory=false;
  lock_init(&inode->inode_xtension_lock);
  list_push_front (&open_inodes, &inode->elem);
//  bcache_read(inode->sector, &inode->data);
  lock_release(&inode_sync_lock);
//  block_read (fs_device, inode->sector, &inode->data);
  
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  lock_acquire(&inode_sync_lock);
  if (inode != NULL)
    inode->open_cnt++;
  lock_release(&inode_sync_lock);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;
  lock_acquire(&inode_sync_lock);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed)
        {
          reload_inode(inode);
          free_map_release (inode->sector, 1);
        struct inode_disk* disk_inode=inode->data;

        uint32_t sectors_left = (uint32_t)bytes_to_sectors((inode->data)->length);
        int i=0;
        for(i=0;i<MAX_ADDRESS_MAIN;i++)
        {
          if(sectors_left==0) break;          
          free_map_release((disk_inode->direct_address[i]),1);
          sectors_left--;
        }
        
        struct address_dedicated_inode_disk* ded_inode=NULL;

        uint32_t newSector=0;

        if(sectors_left>0)
        {
//        free_map_allocate(1,&newSector);
//        disk_inode->indirect_address=newSector;
//        ded_inode=calloc(0,sizeof(struct address_dedicated_inode_disk));
	newSector=disk_inode->indirect_address;
        }
        
//        bcache_write(sector,disk_inode);
        struct bufferelem* buff;
        
 	while(sectors_left>0)
 	{
 	free_map_release(newSector,1);
// 	memset(ded_inode,0,sizeof(struct address_dedicated_inode_disk));
	buff=bcache_get_ref(newSector);
	ded_inode=(struct address_dedicated_inode_disk*)buff->cached_data;
 	int i=0;
 	for(i=0;i<MAX_ADDRESS;i++,sectors_left--)
 	{
   	  if(sectors_left==0) break;
 	  free_map_release((ded_inode->direct_address[i]),1);
 	}
// 	uint32_t flush_sector=newSector;
 	if(sectors_left>0)
 	{
// 	  free_map_allocate(1,&newSector);
//          ded_inode->indirect_address=newSector;
	newSector=ded_inode->indirect_address;
 	}
// 	bcache_write(flush_sector,ded_inode); 	  
        bcache_drop_ref(buff,false); 	
 	}
          
                            
                            
          done_inode(inode,false);                  
        }

      free (inode); 
    }
    lock_release(&inode_sync_lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
      block_sector_t sector_idx = byte_to_sector (inode, offset);

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          bcache_read(sector_idx, buffer + bytes_read);
          //block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
         /* if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          */
          
          //bcache_read(sector_idx, bounce);
          struct bufferelem* buff=bcache_get_ref(sector_idx);
          bounce=(uint8_t*)buff->cached_data;
          //block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
          bcache_drop_ref(buff,false);
          bounce=NULL;
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
//  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  bool disk_full=false;
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      bool extended_write=false;
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
       {
//         printf("EXTENDING  %d %d %d %d sector =%d\n",chunk_size,size,inode_left,offset,inode_get_inumber(inode));
         extended_write=true;
         off_t write_bytes=size<sector_left?size:sector_left;
         disk_full=!inode_extend(inode,offset,write_bytes);
         if(disk_full){
         chunk_size=0; 
         size=0;
         }
         else
         chunk_size=write_bytes;
       }
        
      block_sector_t sector_idx ;
      if(!disk_full){
      sector_idx= byte_to_sector (inode, offset);
      if(sector_idx>=4096) printf("off %d chunk %d %d \n",offset,chunk_size,inode_get_inumber(inode));
//      printf("WRITE TO %d \n",sector_idx);
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          //block_write (fs_device, sector_idx, buffer + bytes_written);
//          printf("Writing at %x \t",sector_idx);
          bcache_write(sector_idx, buffer + bytes_written);
 //         printf("Write Done\n");
        }
      else 
        {
          /* We need a bounce buffer. */
         /* if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          */
          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
//          if (sector_ofs > 0 || chunk_size < sector_left)
//            {
              //bcache_read(sector_idx, bounce);
              struct bufferelem* buff=bcache_get_ref(sector_idx);
              ASSERT(buff->bflags & B_BUSY);
              bounce=(uint8_t*)buff->cached_data;
              memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
              bcache_drop_ref(buff,true);
//            block_read (fs_device, sector_idx, bounce);
//            }
//          else
//          {
            //ASSERT(false);			//This condition is redundant
           // memset (bounce, 0, BLOCK_SECTOR_SIZE);
            //bcache_write (sector_idx, bounce);            
//           }
           
          
          //block_write (fs_device, sector_idx, bounce);
        }
       }

      if(extended_write)
      {
      extended_write=false;      
      inode_update_extend(inode,offset,chunk_size);
      
      } 
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
//  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (struct inode *inode)
{
  lock_acquire(&inode->inode_xtension_lock);
  reload_inode(inode);
  off_t temp = (inode->data)->length;
  done_inode(inode,false); 
  lock_release(&inode->inode_xtension_lock);
  return temp; 
}



void 
inode_set_as_directory(struct inode* inode)
  {
    inode->isDirectory=true;
  }

bool
inode_get_type(struct inode* inode)
  {
    return inode->isDirectory;
  }
  
bool 
inode_check_removed(struct inode* inode)
  {
    return inode->removed;
    
  } 

