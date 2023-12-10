#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <console.h>
#include "devices/input.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/process.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "filesys/file.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "threads/vaddr.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static void syscall_exit (int status);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned pos);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd); 
static int64_t get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static int allocate_fd (struct file *file);
static bool is_valid_address (void *addr);

struct lock filesys_lock;	// Privately added

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	// Init lock for file-related syscalls
	lock_init (&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	switch (f->R.rax) {
		case SYS_HALT:
			PANIC ("Unimplemented syscall syscall_%lld", f->R.rax);
			break;
		case SYS_EXIT:
			syscall_exit (f->R.rdi);
			break;
		case SYS_FORK:
		case SYS_EXEC:
		case SYS_WAIT:
		case SYS_CREATE:
			f->R.rax = syscall_create (f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = syscall_remove (f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = syscall_open (f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = syscall_filesize (f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = syscall_read (f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = syscall_write (f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			syscall_seek (f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = syscall_tell (f->R.rdi);
			break;
		case SYS_CLOSE:
			syscall_close (f->R.rdi);
			break;
		case SYS_MMAP:
		case SYS_MUNMAP:
		case SYS_CHDIR:
		case SYS_MKDIR:
		case SYS_READDIR:
		case SYS_ISDIR:
		case SYS_INUMBER:
		case SYS_SYMLINK:
			PANIC ("Unimplemented syscall syscall_%lld", f->R.rax);
			break;
		case SYS_DUP2:
			break;
		case SYS_MOUNT:
		case SYS_UMOUNT:
			PANIC ("Unimplemented syscall syscall_%lld", f->R.rax);
			break;
		default:
			PANIC ("Unknown syscall syscall_%lld", f->R.rax);
	}
}

static void
syscall_exit (int status) {
	struct thread *curr = thread_current ();
	struct task *task = process_find_by_tid (curr->tid);
	if (task == NULL) {
		return;
	}

	task->exit_code = status;
	thread_exit ();
}

int 
syscall_filesize (int fd) {
	struct thread *curr = thread_current ();
	struct task *task = process_find_by_tid (curr->tid);
	struct fd *fd_info = NULL;
	if (task == NULL) {
		return -1;
	}

	if (fd < 0 || fd >= MAX_FD) {
		return -1;
	}

	fd_info = &task->fds[fd];
	if (fd_info->closed) {
		return -1;
	}

	if (fd_info->file == NULL) {
		return -1;
	}

	return file_length (fd_info->file);
}

static int 
syscall_read (int fd, void *buffer, unsigned size) {
	struct thread *curr = thread_current ();
	struct task *task = process_find_by_tid (curr->tid);
	if (task == NULL) {
		return -1;
	}
	
	if (fd < 0 || fd >= MAX_FD) {
		return -1;
	}

	if (get_user (buffer) == -1 || get_user (buffer + size) == -1) {
		return -1;
	}

	if (fd == 0 && !task->fds[fd].closed) {
		for (size_t i = 0; i < size; i++) {
			bool result = put_user (buffer + i, input_getc());
			if (!result) {
				task->exit_code = 139;
				thread_exit ();
			}
		}

		return size;
	}

	if (task->fds[fd].closed || task->fds[fd].file == NULL) {
		return -1;
	}

	return file_read (task->fds[fd].file, buffer, size);
}

bool 
syscall_create (const char *file, unsigned initial_size) {
	if (!is_valid_address (file)) {
		syscall_exit (-1);
	}

	lock_acquire (&filesys_lock);

	// Call filesys_create to create file
	bool success = filesys_create (file, initial_size);

	lock_release (&filesys_lock);

	return success;
}

bool 
syscall_remove (const char *file) {
	if (!is_valid_address (file)) {
		syscall_exit (-1);
	}

	lock_acquire (&filesys_lock);

	// Call filesys_remove to remove file
	bool success = filesys_remove (file);

	lock_release (&filesys_lock);
	return success;
}

int 
syscall_open (const char *file) {
	if (!is_valid_address (file)) {
		syscall_exit (-1);
	}

	lock_acquire (&filesys_lock);

	// Call filesys_open to open file
	struct file *f = filesys_open (file);
	if (f == NULL) {
		return -1;
	}

	// If opening file is successful, allocate fd to opened file
	int fd = allocate_fd (f);
	if (fd < 0) {
		return -1;
	}
	
	lock_release (&filesys_lock);
	return fd;
}

static int
syscall_write (int fd, void *buffer, unsigned size) {
	struct thread *curr = thread_current ();
	struct task *task = process_find_by_tid (curr->tid);
	if (task == NULL) {
		return -1;
	}

	if (fd < 0 || fd >= MAX_FD) {
		return -1;
	}

	if (get_user (buffer) == -1 || get_user (buffer + size) == -1) {
		return -1;
	}

	if (fd == 1 && !task->fds[fd].closed) {
		putbuf(buffer, size);
		return size;
	}

	if (task->fds[fd].closed || task->fds[fd].file == NULL) {
		return -1;
	}

	return file_write (task->fds[fd].file, buffer, size);
}

static void
syscall_seek (int fd, unsigned pos) {
	struct thread *curr = thread_current ();
	struct task *task = process_find_by_tid (curr->tid);
	if (task == NULL) {
		return;
	}

	if (fd < 0 || fd >= MAX_FD) {
		return;
	}

	if (task->fds[fd].closed || task->fds[fd].file == NULL) {
		return;
	}

	file_seek (task->fds[fd].file, pos);
}

static unsigned
syscall_tell (int fd) {
	struct thread *curr = thread_current ();
	struct task *task = process_find_by_tid (curr->tid);
	if (task == NULL) {
		return -1;
	}

	if (fd < 0 || fd >= MAX_FD) {
		return -1;
	}

	if (task->fds[fd].closed || task->fds[fd].file == NULL) {
		return -1;
	}

	return file_tell (task->fds[fd].file);
}

static void
syscall_close (int fd) {
	struct thread *curr = thread_current ();
	struct task *task = process_find_by_tid (curr->tid);
	if (task == NULL) {
		return;
	}

	if (fd < 0 || fd >= MAX_FD) {
		return;
	}
	
	if (task->fds[fd].closed) {
		return;
	}

	if (fd <= 2 && fd >= 0) {
		task->fds[fd].closed = true;
		return;
	}

	if (task->fds[fd].file == NULL) {
		return;
	}

	file_close (task->fds[fd].file);
	task->fds[fd].closed = true;
	task->fds[fd].file = NULL;
}

/* Reads a byte at user virtual address UADDR.
 * UADDR must be below KERN_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occurred. */
static int64_t
get_user (const uint8_t *uaddr) {
    int64_t result;
    __asm __volatile (
    "movabsq $done_get, %0\n"
    "movzbq %1, %0\n"
    "done_get:\n"
    : "=&a" (result) : "m" (*uaddr));
    return result;
}

/* Writes BYTE to user address UDST.
 * UDST must be below KERN_BASE.
 * Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte) {
    int64_t error_code;
    __asm __volatile (
    "movabsq $done_put, %0\n"
    "movb %b2, %1\n"
    "done_put:\n"
    : "=&a" (error_code), "=m" (*udst) : "q" (byte));
    return error_code != -1;
}

// Allocate empty fd to given file
static int
allocate_fd (struct file *file) {
	struct task *curr_task = process_find_by_tid (thread_current ()->tid);
	
	// Search emtpy fd, start from 3
	int fd = -1;
	for (int i = 3; i <= MAX_FD; i++) {
		if (curr_task->fds[i].closed) {			// If any closed fd is found
			curr_task->fds[i].closed = false;	// Change status of fd to 'open'
			curr_task->fds[i].file = file;		// Set given fild into fd
			fd = i;								// Get fd number
			break;
		}
	}

	return fd;
}

// Validates given address and if not, call exit(-1)
// 'Valid address' means 1. not NULL 2. located in user area
static bool
is_valid_address (void *addr) {
	return addr != NULL && is_user_vaddr (addr) 
	&& pml4_get_page(thread_current ()->pml4, addr) != NULL;
}