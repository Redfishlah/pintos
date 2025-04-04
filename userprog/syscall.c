#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include <devices/shutdown.h>

#include <string.h>
#include <filesys/file.h>
#include <devices/input.h>
#include <threads/malloc.h>
#include <threads/palloc.h>
#include "process.h"
#include "pagedir.h"
#include <threads/vaddr.h>
#include <filesys/filesys.h>

#define MAX_SYSCALL 20

// lab01 Hint - Here are the system calls you need to implement.
struct thread_file * find_file_id(int fd);
static void syscall_handler (struct intr_frame *);
void *check_pointer(const void *vaddr);
bool is_valid_pointer (void* esp,uint8_t argc);
/* System call for process. */

void sys_halt(void);
void sys_exit(struct intr_frame* f);
void sys_exec(struct intr_frame* f);
void sys_wait(struct intr_frame* f);

/* System call for file. */
void sys_create(struct intr_frame* f);
void sys_remove(struct intr_frame* f);
void sys_open(struct intr_frame* f);
void sys_filesize(struct intr_frame* f);
void sys_read(struct intr_frame* f);
void sys_write(struct intr_frame* f);
void sys_seek(struct intr_frame* f);
void sys_tell(struct intr_frame* f);
void sys_close(struct intr_frame* f);


static void (*syscalls[MAX_SYSCALL])(struct intr_frame *) = {
  [SYS_HALT] = (void (*)(struct intr_frame *))sys_halt,
  [SYS_EXIT] = sys_exit,
  [SYS_EXEC] = sys_exec,
  [SYS_WAIT] = sys_wait,
  [SYS_CREATE] = sys_create,
  [SYS_REMOVE] = sys_remove,
  [SYS_OPEN] = sys_open,
  [SYS_FILESIZE] = sys_filesize,
  [SYS_READ] = sys_read,
  [SYS_WRITE] = sys_write,
  [SYS_SEEK] = sys_seek,
  [SYS_TELL] = sys_tell,
  [SYS_CLOSE] = sys_close
};

static void syscall_handler (struct intr_frame *);

/* Method in document to handle special situation */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:" : "=&a" (result) : "m" (*uaddr));
  return result;
}

/*check if the address of the pointer is valid*/
void *check_pointer(const void *vaddr) {
  if (!is_user_vaddr(vaddr) ||
      pagedir_get_page(thread_current()->pagedir, vaddr) == NULL) {
        thread_current()->status_exit = -1;
        thread_exit ();
  }

  
  for (uint8_t i = 0; i < 4; i++) {
      if (get_user((uint8_t *) vaddr + i) == -1) {
        thread_current()->status_exit = -1;
        thread_exit ();
      }
  }

  return pagedir_get_page(thread_current()->pagedir, vaddr);
}

static void
check_pointer_range(const void *vaddr, size_t size)
{
    const uint8_t *addr = vaddr;
    for (size_t i = 0; i < size; i++)
    {
        check_pointer(addr + i);
    }
}

void syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


/* System Call: void halt (void)
    Terminates Pintos by calling shutdown_power_off() (declared in devices/shutdown.h).
*/
void sys_halt(void)
{
  shutdown_power_off();
}

void sys_exit(struct intr_frame *f) {
  uint32_t *user_p = f->esp;
  check_pointer(user_p + 1);
  user_p++;

  thread_current()->status_exit = *user_p; // save exit_code
  thread_exit ();
}

void
sys_exec (struct intr_frame* f)
{
  check_pointer_range(f->esp, 4);
  uint32_t *user_p = f->esp;
  const char *cmd_line = (const char *) user_p[1];

  //check the cmd_line in userspace
  check_pointer (cmd_line);
  f->eax = process_execute(cmd_line);
}

void
sys_wait (struct intr_frame* f)
{
  uint32_t *user_p = f->esp;
  check_pointer (user_p + 1);
  user_p++;
  f->eax = process_wait(*user_p);
}

void
sys_write (struct intr_frame* f)
{
  uint32_t *user_p = f->esp;
  check_pointer (user_p + 7);
  check_pointer ((void *) *(user_p + 6));
  user_p++;
  int fd = *user_p;
  const char * buffer = (const char *)*(user_p+1);
  off_t size = *(user_p+2);
  if (fd == 1) {
    /* Use putbuf to do testing */
    putbuf(buffer,size);
    f->eax = size;//return number written
  }
  else
  {
    /* Write to Files */
    struct thread_file * thread_file_temp = find_file_id (*user_p);
    if (thread_file_temp)
    {
      create_lock();//file operating needs lock
      f->eax = file_write (thread_file_temp->file, buffer, size);
      end_lock();
    }
    else
    {
      f->eax = 0;//can't write,return 0
    }
  }
}

void
sys_create(struct intr_frame* f)
{
  uint32_t *user_p = f->esp;
  check_pointer (user_p + 5);
  check_pointer ((void *) *(user_p + 4));
  user_p++;
  create_lock ();
  f->eax = filesys_create ((const char *)*user_p, *(user_p+1));
  end_lock();
}

void
sys_remove(struct intr_frame* f)
{
  uint32_t *user_p = f->esp;
  check_pointer (user_p + 1);
  check_pointer ((void *) *(user_p + 1));
  user_p++;
  create_lock();
  f->eax = filesys_remove ((const char *)*user_p);
  end_lock();
}

void
sys_open (struct intr_frame* f)
{
  uint32_t *user_p = f->esp;
  check_pointer (user_p + 1);
  check_pointer ((void *) *(user_p + 1));
  user_p++;
  create_lock();
  struct file * file_opened = filesys_open((const char *)*user_p);
  end_lock();
  struct thread * t = thread_current();
  if (file_opened)
  {
    struct thread_file *temp = malloc(sizeof(struct thread_file));
    temp->fd = t->file_fd++;
    temp->file = file_opened;
    list_push_back (&t->files, &temp->file_element);
    f->eax = temp->fd;
  }
  else
  {
    f->eax = -1;
  }
}

/* Find file by the file's ID */
struct thread_file *
find_file_id (int file_id)
{
  struct list_elem *e;
  struct thread_file * temp = NULL;
  struct list *files = &thread_current ()->files;
  for (e = list_begin (files); e != list_end (files); e = list_next (e)){
    temp = list_entry (e, struct thread_file, file_element);
    if (file_id == temp->fd)
      return temp;
  }
  return false;
}


void
sys_filesize (struct intr_frame* f){
  uint32_t *user_p = f->esp;
  check_pointer (user_p + 1);
  user_p++;
  struct thread_file * temp = find_file_id (*user_p);
  if (temp)
  {
    create_lock();
    f->eax = file_length (temp->file);//return the size in bytes
    end_lock();
  }
  else
  {
    f->eax = -1;
  }
}

/*Check if the user pointer is valid*/
bool
is_valid_pointer (void* esp,uint8_t argc){
  for (uint8_t i = 0; i < argc; ++i)
  {
    if((!is_user_vaddr (esp)) ||
      (pagedir_get_page (thread_current()->pagedir, esp)==NULL)){
      return false;
    }
  }
  return true;
}

/* system read*/
void
sys_read (struct intr_frame* f)
{
  //check stack for at least 12 bytes
  check_pointer_range((uint8_t*)f->esp, 12);
  uint32_t *user_p = f->esp;
  int fd = user_p[1];
  uint8_t * buffer = (uint8_t*)user_p[2];
  off_t size = user_p[3];
  //check size
  if (size == 0) {
    f->eax = 0;
    return;
  }
  //check buffer
  check_pointer_range(buffer, size);

  //check fd
  if (fd == 0)
  {
    for (int i = 0; i < size; i++)
      buffer[i] = input_getc();
    f->eax = size;
  }
  else
  {
    struct thread_file * temp = find_file_id (fd);
    if (temp)
    {
      create_lock();
      f->eax = file_read (temp->file, buffer, size);
      end_lock();
    }
    else
    {
      f->eax = -1;
    }
  }
}

/* system seek */
void
sys_seek(struct intr_frame* f)
{
  uint32_t *user_p = f->esp;
  check_pointer (user_p + 5);
  user_p++;
  struct thread_file *temp = find_file_id (*user_p);
  if (temp)
  {
    create_lock();
    file_seek (temp->file, *(user_p+1));
    end_lock();
  }
}

/* system tell */
void
sys_tell (struct intr_frame* f)
{
  uint32_t *user_p = f->esp;
  check_pointer (user_p + 1);
  user_p++;
  struct thread_file *temp = find_file_id (*user_p);
  if (temp)
  {
    create_lock();
    f->eax = file_tell (temp->file);
    end_lock();
  }else{
    f->eax = -1;
  }
}

void
sys_close (struct intr_frame* f)
{
  //uint32_t *user_p = f->esp;
  check_pointer((uint8_t*)f->esp + 4 - 1);
  int fd = *(((int*) f->esp) + 1);
  /* stdin(0)  or stdout(1) ignore */
  if (fd == 0 || fd == 1)
    return;
  //check_pointer (user_p + 1);
  //user_p++;
  struct thread_file * opened_file = find_file_id (fd);
  if (opened_file)
  {
    create_lock();
    file_close (opened_file->file);
    end_lock();
    /* Remove the opened file from the list */
    list_remove (&opened_file->file_element);
    /* Free opened files */
    free (opened_file);
  }else{
    return;
  }
}

static void syscall_handler (struct intr_frame *f UNUSED)
{
  //printf ("system call!\n");
  int * p = f->esp;
  check_pointer (p + 1);//check the first argument
  int type = * (int *)f->esp;//check the type
  if(type <= 0 || type >= MAX_SYSCALL){
    thread_current()->status_exit = -1;
    thread_exit ();
  }
  syscalls[type](f);
  //thread_exit();
}
