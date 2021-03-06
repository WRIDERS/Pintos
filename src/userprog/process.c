#include "userprog/process.h"
//18838
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/pte.h"
#include "vm/page.h"
#include "vm/filetracker.h"
#include "vm/frame.h"




static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
bool dump_one_page(struct file* file_ptr, uint8_t *kpage,off_t offset,uint32_t fillBytes);


/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{

  char *fn_copy,*fn_thrd_copy;
  tid_t tid;


  if(strlen(file_name)>PGSIZE/2)       //Too big file name leaves no space on stack 
  return TID_ERROR;
  
  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  fn_thrd_copy = palloc_get_page (0);

  
  if (fn_copy == NULL)
    return TID_ERROR;


  strlcpy (fn_copy, file_name, PGSIZE);
  strlcpy (fn_thrd_copy, file_name, PGSIZE);

  
  char * save_ptr;  				//Giving correct thread name
  fn_thrd_copy=strtok_r(fn_thrd_copy," ",&save_ptr);





  /* Create a new thread to execute FILE_NAME. */
  intr_disable();
  tid = thread_create (fn_thrd_copy, PRI_DEFAULT, start_process, fn_copy);
  if(tid!=TID_ERROR )
    {
      struct thread* t=get_by_tid(tid);
      if(thread_current()->myDir!=NULL)
      t->myDir=dir_reopen(thread_current()->myDir);
      else
      t->myDir=dir_open_root();      
    }  
  intr_enable();

  if (tid == TID_ERROR)
    palloc_free_page (fn_copy);
        
  palloc_free_page (fn_thrd_copy);
    
        
  return tid;
  
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  char *save_ptr;

  int name_size=strlen(file_name);		//storing size for future use

  file_name=strtok_r(file_name," ",&save_ptr);	//get refined file name


  
  success = load (file_name, &if_.eip, &if_.esp);
  /* If load failed, quit. */



  if (!success) {				//if load failed free used  resources and exit
//  printf("LOAD FAILED\n");
    palloc_free_page (file_name);
    thread_current()->return_status=-1;
    (thread_current()->parent)->childLoad=false;    
    sema_up(&thread_current()->exec);    
    thread_exit ();
  }


  //Denying write to executables
//  lock_acquire(&file_sync_lock);
  thread_current()->myExecutable=filesys_open(file_name);
//  lock_release(&file_sync_lock);
  
  if(thread_current()->myExecutable!=NULL)
  {
   thread_current()->myFile=file_get_write_status(thread_current()->myExecutable);
   file_deny_write(thread_current()->myExecutable);
  }


  
  (thread_current()->parent)->childLoad=true;
  sema_up(&thread_current()->exec);





//MY IMPLEMENTATION STARTS FROM HERE
  char* stk_ptr=(char*)if_.esp;			//Getting user stack pointer
  int i=0;
  bool record=false;  
  
  file_name[strlen(file_name)]=' ';		//undo the effect of strtok_r
  file_name[name_size]='\0';			//don't overdo it in case of none-args
    

/*
This for loop analyses every character in 
file_name and pushes them on user stack one by one 
*/

  for (i=strlen(file_name)-1;i>=0;i--)
  {
   if(file_name[i]!=' ' && record==false)
    {
      stk_ptr--;
      *stk_ptr='\0';
      record=true;
    }
    else if(file_name[i]==' ')
     record=false;

    if(record)						//if recording mode is on i.e. current chars are not equal to ' '
    {
     stk_ptr--;
      *stk_ptr=file_name[i];
    }		   		
  }


//Aligning it to granularity of 4 bytes
 int padding=((char*)if_.esp-stk_ptr)%4;
 stk_ptr-=(4-padding);




//Now is the time to push integer addresses on stack
 int* stk_ptr_int=(int*)stk_ptr;

//NULL TERMINATOR for argv[argc]
 stk_ptr_int--;
 *stk_ptr_int=0;

//load references here

 stk_ptr=if_.esp-1;	//reload stk_ptr

/*
This for loop analyses the user stack from top and 
pushes the address of arguments one by one
*/

 bool start =false;
 int argc=0;
 for(;(int)stk_ptr>=(int)(stk_ptr_int+1);stk_ptr--)
 {
  if(*stk_ptr!=0)
   start=true;
  else
  {
   if(start)
   {
    *(--stk_ptr_int)=(int)(stk_ptr+1);argc++;	//Saving address of argument 
   }
   start=false;
  }
 }


// pushing **argv
 stk_ptr_int--;
 *stk_ptr_int=(int)stk_ptr_int+4;	

// pushing argc
 stk_ptr_int--;
 *stk_ptr_int=argc;


//pushing Fake Return address
 stk_ptr_int--;
 *stk_ptr_int=0;



  

 if_.esp = stk_ptr_int; 		//Pointing user stack in trapframe to updated stack pointer

   
  
 palloc_free_page (file_name);

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
   
int
process_wait (tid_t child_tid UNUSED) 
{

  struct thread *req=get_by_tid(child_tid);
	while(true){
	
	 if(req==NULL||req->status== THREAD_DYING){
	 	struct zombie_thread * ff=dead_child(child_tid);
	 
	 	if(ff==NULL){
	 		return -1;
	 	}
	 	int status=ff->return_status;
	 	
	 	
	 	 printf("%s: exit(%d)\n",ff->name,status);
	 	 zombie_remove(ff);
	 	 
	 	return status;
	 }
	        
		sema_down(&(thread_current()->wait));
		req=get_by_tid(child_tid);

		
	}
	
}


/* Free the current process's resources. */

void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;
  

  if(thread_current()->myExecutable!=NULL)
  {
    if(thread_current()->myFile)
      file_close(thread_current()->myExecutable);
    thread_current()->myExecutable=NULL;
  }
  
  
    
     


   //frees supplemental page_table
  lock_acquire(&frame_lock);  
  free_supp_page(&cur->sup_pagelist);
  pd = cur->pagedir;
  if (pd != NULL)
    {
      
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);


    }
  lock_release(&frame_lock);
    
  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  if(cur->parent!=NULL){
	add_zombie(); 
  }
    
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in //printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;
	
  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  strlcpy (t->filename, file_name, strlen(file_name)+1);
  if (t->pagedir == NULL) 
    goto done;
    
  process_activate ();

  /* Open executable file. */
//  lock_acquire(&file_sync_lock);
  file = filesys_open (file_name);
//  lock_release(&file_sync_lock);

  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

//  printf("READING MAIN FILE NAMED %s \n",file_name);
  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
//    printf("REACHED\n");
      goto done; 
    }
  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
      goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
      goto done;
        
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
                
              
              if(!populate_supp_pgdir(t->pagedir,(void *) mem_page,file_page,writable,FILE_SYS,read_bytes,zero_bytes,t->myExecutable,&(t->sup_pagelist)))
              {
              goto done; 
              }
            }
          else
          goto done;
          break;
        }
    }
  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
//  lock_acquire(&file_sync_lock);
  file_close (file);
//  lock_release(&file_sync_lock);
  
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
  return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
  return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
  return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
  return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
  return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
  return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
  return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
      if (phdr->p_vaddr < PGSIZE)
      return false;
  

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);
//  lock_acquire(&file_sync_lock);
  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      lock_acquire(&frame_lock);
      uint8_t *kpage = allocate_user_frame(upage);//palloc_get_page (PAL_USER);
      if (kpage == NULL)
      {
      lock_release(&frame_lock);
//      lock_release(&file_sync_lock);
        return false;
	}
      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          deallocate_user_frame(kpage);
          lock_release(&frame_lock);
//          lock_release(&file_sync_lock);
          
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable))
        {
          deallocate_user_frame(kpage);
          lock_release(&frame_lock);
//          lock_release(&file_sync_lock);
          return false; 
        }
      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      lock_release(&frame_lock);

    }
//   lock_release(&file_sync_lock);

  return true;
}



/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;
  lock_acquire(&frame_lock);
  kpage = allocate_user_frame(((uint8_t *) PHYS_BASE) - PGSIZE);//palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      
      if (success)
      {
        *esp = PHYS_BASE;
        thread_current()->allocStack=(uint32_t)(PHYS_BASE-PGSIZE);
      }
      else
      deallocate_user_frame(kpage);
      
    }
   lock_release(&frame_lock); 
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

// loads 1 page from file_system
bool load_one_page(uint32_t* pagedir, uint8_t *upage)
{
	
	struct fileMetadata* MD = filetracker_check((uint32_t)upage,(uint32_t)pagedir);
	ASSERT(MD!=NULL);
	uint32_t adr_sup_tabl=*((uint32_t *)pagedir_get_page_raw (pagedir, upage));
	struct supp_page *pp=(struct supp_page *)(adr_sup_tabl & ~0x3);//leaving out two bits
	ASSERT(pp->v_page <= (uint32_t)upage);
	bool write_able=((adr_sup_tabl & PTE_W)==PTE_W);
	
	
	
	struct file * fp = MD->file_ptr;//filesys_open (file_name);
	if (fp==NULL) {
		fp=thread_current()->myExecutable;
	}
	 
	
	clear_before_load(pagedir,(void *) upage);
	
	
	bool x= load_segment (fp,pp->file_page,(uint8_t *) pp->v_page, pp->read_byte, pp->zero_byte, write_able);

	return x;
}

/*Writes a single page to the specified dile_ptr at offset 
*/
bool
dump_one_page(struct file* file_ptr, uint8_t *kpage,off_t offset,uint32_t fillBytes)
{

if(thread_current()->myExecutable==file_ptr)
{
return false;										//Changes to programs data segment
}											//write to swap 

ASSERT(file_ptr!=NULL);
struct file* file=file_ptr;//filesys_open(file_name);
ASSERT(file!=NULL);

file_seek(file,offset);
off_t wBytes=file_write(file,(const void *)kpage,fillBytes);
ASSERT(wBytes==(off_t)fillBytes);
return true;
}


