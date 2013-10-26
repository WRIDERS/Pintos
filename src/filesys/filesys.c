#include "filesys/filesys.h"
//7227
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/bcache.h"
#include "threads/thread.h"
#include "threads/malloc.h"


/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);
struct dir* filesys_get_dir(const char* original_path,bool *original,char* prunedName);

#define CONST_DIR_SIZE 0


/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */


void
filesys_init (bool format) 
{
  
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");
  
  bcache_init(fs_device);	//Buffer Cache should be initialized befor any read/write to disk happens
  inode_init ();
  free_map_init ();
//  lock_init(&file_sync_lock);
  if (format) 
    do_format ();

  free_map_open ();
  dir_init();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void)
{
  free_map_close ();
  bcache_flush(true);
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;
  bool close=false;
  char* buf_name=(char*)calloc(1,strlen(name)+1);
  struct dir *dir = filesys_get_dir(name,&close,buf_name);
///  printf("CREATING %s dir= %x\n",name,dir);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, buf_name, inode_sector));

  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);

  if(close)
  dir_close (dir);
  
  free(buf_name);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  bool close=false;
  char* buf_name=(char*)calloc(1,strlen(name)+1);

  struct dir *dir = filesys_get_dir(name,&close,buf_name);
  struct inode *inode = NULL;
//  printf("OPENING %s \n",buf_name);
  if (dir != NULL)
    dir_lookup (dir, buf_name, &inode);

  if(close)
  dir_close (dir);

  free(buf_name);  
  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name)
{
  bool close=false;
  char* buf_name=(char*)calloc(1,strlen(name)+1); 
  struct dir *dir = filesys_get_dir(name,&close,buf_name);
  
  bool success = dir != NULL && dir_remove (dir, buf_name);  

  if(close)
  dir_close (dir); 
  
  free(buf_name);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, CONST_DIR_SIZE))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

bool
create_directory(const char *dirname)
  {
  
    bool success;
    if(!(success=filesys_create(dirname,CONST_DIR_SIZE)))
      return false;
    char* buf_name;
    buf_name=(char*)calloc(1,strlen(dirname)+1);
    bool ff=false;

    struct dir* install_directory=filesys_get_dir(dirname,&ff,buf_name);
    
    struct inode* new_dir_inode;
    dir_lookup(install_directory,buf_name,&new_dir_inode);
    dir_set_directory_type(install_directory,buf_name);
    
    struct dir* new_dir=dir_open(new_dir_inode);
    
    dir_add(new_dir,".",inode_get_inumber(dir_get_inode(new_dir)));
    dir_add(new_dir,"..",inode_get_inumber(dir_get_inode(install_directory)));
    
    dir_close(new_dir);
    
    if(ff)
    dir_close(install_directory);
    
    return success;

  }


struct dir* 
get_dir(const char* dir_name)
  {
    char* buf_name;
    buf_name=(char*)calloc(1,strlen(dir_name)+1);
    bool ff=false;

    struct dir* parent_directory=filesys_get_dir(dir_name,&ff,buf_name);
    struct inode* dir_inode;

    dir_lookup(parent_directory,buf_name,&dir_inode);

    struct dir* get_dir=dir_open(dir_inode);
    return get_dir;    
  }

/**/
struct dir* 
filesys_get_dir(const char* original_path,bool *original,char* prunedName)
  {
/*
    printf("TESTING\n");

    char buf[20]="home/root";
    char* buff=(char*)calloc(1,20);
    strlcpy(buff,buf,20);
    char * test_token,test_save_ptr;
    test_token=strtok_r(buff,"/",&test_save_ptr);
    printf("Token 1 = %s \n",test_token);
    test_token=strtok_r(NULL,"/",&test_save_ptr);
    printf("Token 2 = %s \n",test_token);
    test_token=strtok_r(NULL,"/",&test_save_ptr);
    printf("Token 3 = %s \n",test_token);
    
    
    printf("TESTING DONE\n");
*/
  
  
     char* path=(char*)calloc(1,strlen(original_path)+1);
     strlcpy(path,original_path,strlen(original_path)+1);
//     printf("GET DIR FOR %s \n",original_path);
     
     char *prevToken,*nextToken,*save_ptr;
     
     bool newOpen=false;

     struct dir* cur_dir=NULL;
     
     prevToken=strtok_r(path,"/",&save_ptr);
     nextToken=strtok_r(NULL,"/",&save_ptr);

     if(prevToken==NULL && *original_path=='/')
     prevToken=".";

     if(prevToken==NULL)
         goto done;


     
     cur_dir=thread_current()->myDir;
     struct inode* inode=NULL;
     if(cur_dir==NULL || *original_path=='/')
       {
             cur_dir=dir_open_root();         
             newOpen=true;
       }

     
//     printf("prevToken %s nextToken %s\n",prevToken,nextToken);
     
     
     while(nextToken!=NULL)
       {
         if(!dir_lookup(cur_dir,prevToken,&inode))
         {
           if(newOpen) dir_close(cur_dir);
           cur_dir=NULL;
           goto done;
         }
         if(newOpen)dir_close(cur_dir);
         cur_dir=dir_open(inode);
         newOpen=true;
         prevToken=nextToken;
         nextToken=strtok_r(NULL,"/",&save_ptr);
       }
       
       if(prunedName!=NULL && prevToken!=NULL)
         strlcpy(prunedName,prevToken,strlen(prevToken)+1);

     done:
     *original=newOpen;
     free(path);
     return cur_dir;
  }


bool
directory_read(struct file* file_ptr,char* buf)
  {
    if(!file_get_type(file_ptr))
    return false;
//    printf("READDIR %x\n",file_get_dir(file_ptr));
   return  dir_readdir(file_get_dir(file_ptr),buf);
  }







