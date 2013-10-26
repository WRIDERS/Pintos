#include "userprog/syscall.h"
#include "userprog/pagedir.h"
//14406
#include <stdio.h>
#include <syscall-nr.h>
#include "lib/string.h"
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/pte.h"
#include "threads/init.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/filetracker.h"
#include "filesys/file.h"
#include "filesys/filesys.h"



static void syscall_handler (struct intr_frame *);
static int get_user (const uint8_t *uaddr);
static int read_arg32(const uint8_t *uaddr);// reads one argument (32 bit)
static int fill_arg( uint32_t *array1,int sys_num, uint8_t * esp); //fills 32bit arg-values into array1 and returns no. of args.
bool invalid(const uint8_t * uaddr,bool gonnaWrite);


void
syscall_init (void)
{

  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{


	   	uint32_t syscall_num=read_arg32((uint8_t *)(f->esp));	//Potential page fault  
	   	uint32_t arguments[MAX_ARG_SYS]; //IT STORES THE ARGUEENTS
  		fill_arg(arguments,syscall_num,(uint8_t *) (f->esp));

		//Saving User stack pointer for stack growth
	    thread_current()->user_esp=f->esp;

   
  if(syscall_num==SYS_EXIT)
  {
	struct thread * t=thread_current();
	t->return_status=arguments[0];
	thread_exit();
  }
  else if(syscall_num==SYS_WAIT)
  {
	int pid_t=arguments[0];  
        int result=process_wait(pid_t);  
	f->eax=result;    
  }
  else if(syscall_num==SYS_EXEC)
  {
	  int pid_t=-1;


	  char * cmdline=(char *)arguments[0];
	  
	  if(invalid((const uint8_t *)cmdline,false)){f->eax=-1;return;}
	  
	  if(*cmdline!=0)
	  {  
	    pid_t=process_execute(cmdline);

	    if(pid_t!=TID_ERROR && pid_t!=-1)
	     {
	  
	      sema_down(&get_by_tid(pid_t)->exec);           /// Parent is 

	      if(!thread_current()->childLoad)
	      pid_t=-1;
	    }
	  }
	  else 
	  pid_t=-1;


	  f->eax=pid_t;    

  }
  else if (syscall_num==SYS_HALT)
  {
  	power_off();
  }
  else if(syscall_num==SYS_WRITE)
  {
  	int fd_no=arguments[0];
  	const void* buffer=(const void*)arguments[1];
  	unsigned size=arguments[2];
  	int result=0;
  	

	if(invalid(buffer,false)){f->eax=-1; return;};
	if(fd_no<0 || fd_no>MAXFILES){f->eax=-1;return;}
        if(size==0){f->eax=0;return;}

		
	if(fd_no==STDIN_FILENO)
	 result=-1;
	else if(fd_no==STDOUT_FILENO)   //writing to console
	{
 	 putbuf(buffer,size);  		
	 result=size;
	}
	else				//Writing on file 
	{

	if(thread_current()->fd[fd_no]!=NULL)
 	 {
 	 if(file_get_type(thread_current()->fd[fd_no]))
 	 {
 	   f->eax=-1;
 	   return;
 	 }
   	  uint8_t* tempBuffer=palloc_get_page(0);
   	  int dataBytes=size;
   	  int curPos=0;
 
   	  while(dataBytes>0)
   	  {
   	  memset(tempBuffer,0,PGSIZE);
   	  int writeNow=dataBytes>PGSIZE?PGSIZE:dataBytes;

  	  memcpy(tempBuffer,buffer+curPos,writeNow);

//	  lock_acquire(&file_sync_lock);
	  result+=file_write((struct file*)((int *)((thread_current()->fd[fd_no]))),tempBuffer,writeNow);
//	  lock_release(&file_sync_lock);
	  curPos+=writeNow;
	  dataBytes-=writeNow;
	  }
	  palloc_free_page(tempBuffer);
	 } 
 	 else
	 result=-1;
	
	}
	f->eax=result;
  }
  else if (syscall_num==SYS_READ)
  {
  	int fd_no=(int)arguments[0];
  	char* buffer=(char*)arguments[1];
  	unsigned size=arguments[2];
  	
	int result=0;	
        
	if(invalid((const uint8_t *)buffer,true)){f->eax=-1;return;}
	if(fd_no<0 || fd_no>MAXFILES){f->eax=-1;return;}

	if(fd_no==STDIN_FILENO)
	{
	  unsigned i=0;
	  for(;i<size;i++)
	   {
	    buffer[i]=input_getc();
	    result++;
	   }
	}
	else if(fd_no==STDOUT_FILENO)   //writing to console
	 result=-1;
	else				//Writing on file 
	{

	if(thread_current()->fd[fd_no]!=NULL)
 	 {
   	  uint8_t* tempBuffer=palloc_get_page(0);
   	  int dataBytes=size;
   	  int curPos=0;
   	  while(dataBytes>0)
   	  {
   	  memset(tempBuffer,0,PGSIZE);
   	  int readNow=dataBytes>PGSIZE?PGSIZE:dataBytes;

//	  lock_acquire(&file_sync_lock);
	  result+=file_read((struct file*)((int *)((thread_current()->fd[fd_no]))),tempBuffer,readNow);
//	  lock_release(&file_sync_lock);

   	  
   	  memcpy(buffer+curPos,tempBuffer,readNow);
	  
	  curPos+=readNow;
	  dataBytes-=readNow;

	  }
	  palloc_free_page(tempBuffer);
	 }
	else
	 result=-1;
	
	}
	f->eax=result;
  }
  else if(syscall_num==SYS_OPEN)
  {
	   const char* filename = (const char*)arguments[0];
           
	   int result;
	   int i=2;

           if(invalid((const uint8_t *)filename,false)){f->eax=-1;return;}
	   if(*filename==0){f->eax=-1;return;}
	   
	   while(thread_current()->fd[i]!=NULL && i<MAXFILES)
	   i++;
	   
	   if(i==MAXFILES-1)
	   result=-1;	   
	   else
	   {
//	    lock_acquire(&file_sync_lock);
	    void * res=filesys_open(filename);
//	    lock_release(&file_sync_lock);
	   
	    if(res==NULL)
	    {
	     result=-1;
	    }
	    else
	    {
	     thread_current()->fd[i]=res;
	     result=i;
	    }
	   }
	   	   
	   f->eax=result;   
  }
  else if (syscall_num==SYS_CLOSE)
  {
	   int fd_no=arguments[0];	   	   
	   if(fd_no>=2 && fd_no<=MAXFILES)
	    {
	     file_close(thread_current()->fd[fd_no]);
	     thread_current()->fd[fd_no]=NULL;   
	    }
  }
  else if (syscall_num==SYS_CREATE)
  {
	  const char* file=(const char*)arguments[0];
	  int initialsize =arguments[1];
	  bool result;

	  if (invalid(( const uint8_t *)file,false)){f->eax=0;return;}
	  
	  if(*file!=0 && initialsize>=0)
	   {
//	    lock_acquire(&file_sync_lock);
	    result=filesys_create(file,initialsize);
//	    lock_release(&file_sync_lock); 
	   }
	  else
	   result=false;
	  
	  f->eax=(int)result;  
  }
  else if(syscall_num==SYS_REMOVE)
  {
	  const char* file =(const char*) arguments[0];
	  bool result=false;
	
	  if(invalid((const uint8_t *)file,false)){f->eax=0;return;}
	  	  
	  if((*file)!=0)
	  {
//	   lock_acquire(&file_sync_lock);
	   result=filesys_remove(file);
//	   lock_release(&file_sync_lock);
	  // result=true;
	  }
	  else
	   result=false;


	  f->eax=(int)result;    	  
  }
  else if (syscall_num==SYS_FILESIZE)
  {
    int fd_no=arguments[0];
       
    if(fd_no<2 && fd_no>MAXFILES-1) {f->eax=-1;return;}
   
    int result=file_length((struct file*)(thread_current()->fd[fd_no]));
    
    f->eax=result;
    
  }
  else if (syscall_num==SYS_SEEK)
  {  
   int fd_no=arguments[0];
   int pos=arguments[1];
   if(fd_no<2 && fd_no>=MAXFILES){return;}  
   file_seek(thread_current()->fd[fd_no],pos);  
  }
  else if(syscall_num==SYS_TELL)
  {
   int fd_no=arguments[0];
   if(fd_no<2 && fd_no>=MAXFILES){f->eax=-1;return;}
   f->eax=file_tell(thread_current()->fd[fd_no]);
  }
  else if (syscall_num==SYS_MMAP)
  {
   
   int fd_no=arguments[0];
   uint8_t* addr=(uint8_t* )arguments[1];
   struct thread* t=thread_current();

   if(((uint32_t)addr)%PGSIZE!=0)
   {
    f->eax=-1;
    return;
   }
   
   if(fd_no<2 && fd_no >=MAXFILES)
   {
    f->eax=-1;
    return;
   }
 
   if(t->fd[fd_no]==NULL)
   { 
    f->eax=-1;
    return;
   }
   
   uint32_t size=file_length(t->fd[fd_no]);
   if(size==0)
   {
    f->eax=-1;
    return;      
   }
   
   uint32_t nPage=size/PGSIZE+ (size%PGSIZE?1:0);
   uint32_t i=0;
   for(;i<nPage;i++)
   {
      int vaddr=(int)(addr+i*PGSIZE);
      if(is_user_vaddr((const void *)vaddr) && (uint32_t)vaddr!=0)
      {
        if(!pagedir_check_free(t->pagedir,(const void *)vaddr))
        {
        f->eax=-1;
        return;
        }
      }
      else
      {
       f->eax=-1;
       return;
      }
   }
   
   
   populate_supp_pgdir(t->pagedir,(const void *) addr,0,
   true,FILE_SYS,size,nPage*PGSIZE-size ,file_reopen(t->fd[fd_no]),&(t->sup_pagelist));
   ASSERT(filetracker_check((uint32_t)addr,(uint32_t)t->pagedir)!=NULL);
   t->mapID[fd_no]=(int)addr;
   f->eax=(fd_no);
   
   
   
  }
  else if(syscall_num==SYS_MUNMAP)
  {
   uint32_t map_no=arguments[0];
   struct thread* t=thread_current();
   
   if(map_no<2 && map_no >=MAXFILES)
   {
    f->eax=-1;
    return;
   }
   if(t->mapID[map_no]==0)
   {
    f->eax=-1;
    return;
   }



  uint32_t addr=t->mapID[map_no];
  struct fileMetadata* MD=filetracker_check(addr,(uint32_t)t->pagedir);
  struct file* f = MD->file_ptr;
  uint32_t size = file_length(f);
  
  uint32_t nPage=size/PGSIZE+ (size%PGSIZE?1:0);
  uint32_t i=0;
  lock_acquire(&frame_lock);
  for(;i<nPage;i++)
  {
     deallocate_user_mapped_frame((uint32_t)t->pagedir,addr);
     addr+=PGSIZE;
  }
  lock_release(&frame_lock);
  
  t->mapID[map_no]=0;
    
    
  }
  else if(syscall_num==SYS_CHDIR)
  {
    const char * dir = arguments[0];
    struct dir* new_dir=get_dir(dir);
    struct dir* prev_dir=thread_current()->myDir;
    if(new_dir!=NULL)
    {
      thread_current()->myDir=new_dir;
      dir_close(prev_dir);
    }
    f->eax=(new_dir!=NULL);
  }
  else if(syscall_num==SYS_MKDIR)
  {
    const char* dir = arguments[0];
    bool success=create_directory(dir);
    f->eax=success;
  }
  else if(syscall_num==SYS_ISDIR)
  {
   int fd_no=arguments[0]; 
   
   f->eax = file_get_type(thread_current()->fd[fd_no]);
  }
  else if(syscall_num==SYS_READDIR)
  {
    int fd_no=arguments[0];
    char* name=arguments[1];
//    printf("fd_no %d name %s \n",fd_no,name);
    f->eax=directory_read(thread_current()->fd[fd_no],name);
  }
  else if (syscall_num==SYS_INUMBER)
  {
    int fd_no=arguments[0];    
    f->eax=inode_get_inumber(file_get_inode(thread_current()->fd[fd_no]));
  }
  
  thread_current()->user_esp=0;
}


/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */

static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movzbl %1, %0;"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}


bool invalid(const uint8_t * uaddr,bool gonnaWrite){

	struct thread * tt=thread_current();
	uint32_t * pte=pagedir_get_page_raw (tt->pagedir, (const void *)uaddr);
	
	if(!is_user_vaddr (uaddr)||uaddr==0||pte==NULL){
	 thread_current()->return_status=-1;
	 thread_exit();
	 return true;
	}
	if(*pte==0)
	{
 	 thread_current()->return_status=-1;
 	 thread_exit();
	 return true;	
	}
	
	if(gonnaWrite)
	{
	 if (!((*pte & PTE_W)==PTE_W))
	 {
 	 thread_current()->return_status=-1;
 	 thread_exit();
	 return true;		  	 
	 }
	}	
	return false;
	
}

static  int read_arg32(const uint8_t *uaddr){

	int i=0;
	int addr=0;
	int temp=0;
	for(;i<4;i++){
	invalid(uaddr,false);
	temp=get_user(uaddr);
		uaddr++;
		addr=addr|(temp<<8*i);
	}
	return addr;
}
static int fill_arg( uint32_t *array1,int sys_num, uint8_t *esp){ 
	int arg_num;
	 switch (sys_num) {
		
			case SYS_HALT : arg_num=0;	break;
			case SYS_EXIT : arg_num=1;	break;				
			case SYS_EXEC : arg_num=1;	break;	
			case SYS_WAIT : arg_num=1;	break;	
			case SYS_CREATE : arg_num=2;	break;	
			case SYS_REMOVE : arg_num=1;	break;
			case SYS_OPEN : arg_num=1;	break;
			case SYS_FILESIZE : arg_num=1;	break;				
			case SYS_READ : arg_num=3;	break;	
			case SYS_SEEK : arg_num=2;	break;	
			case SYS_WRITE : arg_num=3;	break;
			case SYS_TELL : arg_num=1;	break;
			case SYS_CLOSE : arg_num=1;	break;				
			case SYS_MMAP : arg_num=2;	break;	
			case SYS_MUNMAP : arg_num=1;	break;	
			case SYS_CHDIR : arg_num=1;	break;	
			case SYS_MKDIR : arg_num=1;	break;
			case SYS_READDIR : arg_num=2;	break;				
			case SYS_ISDIR : arg_num=1;	break;	
			case SYS_INUMBER : arg_num=1;	break;
				default : arg_num=-1;	break;
	
			}
	ASSERT(arg_num>=0);
	
	int base =0;
	switch (arg_num) { ///ubuntu 12.04 specific
		case 0 : base=4; break;
		case 1 : base=4; break;
		case 2 : base= 4; break;
		case 3 : base= 4 ; break;
		default : base =-1; 
	
	}
	ASSERT(base >-1);	
	int i=0;
	for(;i<arg_num;i++){
		array1[i]=  read_arg32(esp +base +4*i);	
	}
	return arg_num;
}







