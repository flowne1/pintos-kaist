#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "vm/vm.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// Process-related syscalls
static void syscall_halt (void);
void syscall_exit (int status);
static tid_t syscall_fork (const char *thread_name, struct intr_frame *f);
static int syscall_exec (const char *cmd_line);
int syscall_wait (tid_t tid);

// File-related syscalls
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned pos);
static unsigned syscall_tell (int fd);
void syscall_close (int fd);
static void *syscall_mmap (void *addr, size_t length, int writable, int fd, off_t offset);
static void syscall_munmap (void *addr);

// Helper functions for syscalls
static bool is_valid_addr (void *addr);
// static bool is_valid_buffer (void *addr);
static bool is_writable_page (void *addr);
static bool is_valid_fd (int fd);
static int allocate_fd (struct file *file);
struct file *find_file_by_fd (int fd);

struct lock filesys_lock;

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

	lock_init (&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	switch (f->R.rax) {
		case SYS_HALT:
			syscall_halt ();
			break;
		case SYS_EXIT:
			syscall_exit (f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = syscall_fork (f->R.rdi, f);
			break;
		case SYS_EXEC:
			f->R.rax = syscall_exec (f->R.rdi);
			break;
		case SYS_WAIT:
			f->R.rax = syscall_wait (f->R.rdi);
			break;
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
			f->R.rax = syscall_mmap (f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
			break;
		case SYS_MUNMAP:
			syscall_munmap (f->R.rdi);
			break;
		// case SYS_CHDIR:
		// case SYS_MKDIR:
		// case SYS_READDIR:
		// case SYS_ISDIR:
		// case SYS_INUMBER:
		// case SYS_SYMLINK:
		// case SYS_DUP2:
		// case SYS_MOUNT:
		// case SYS_UMOUNT:
		default:
			PANIC ("Unknown syscall syscall_%lld", f->R.rax);
			break;
	}
}

static void 
syscall_halt (void) {
	power_off ();
}

void 
syscall_exit (int status) {
	struct thread *curr = thread_current ();
	curr->exit_status = status;
	printf ("%s: exit(%i)\n", curr->name, curr->exit_status);
	thread_exit ();
}

static tid_t 
syscall_fork (const char *thread_name, struct intr_frame *f) {
	return process_fork (thread_name, f);
}

static int 
syscall_exec (const char *cmd_line) {
	if (!is_valid_addr (cmd_line)) {
		syscall_exit (-1);
	}

	int file_size = strlen (cmd_line) + 1;			// Given file name + null sentinel
	char *fn_copy = palloc_get_page (PAL_ZERO);		// Allocate single page
	if (!fn_copy) {
		syscall_exit (-1);
	}
	strlcpy (fn_copy, cmd_line, file_size);			// Copy file name 

	if (process_exec (fn_copy) == -1) {				// Execute process
		syscall_exit (-1);
	}
}

int 
syscall_wait (tid_t tid) {
	process_wait (tid);
}

static bool 
syscall_create (const char *file, unsigned initial_size) {
	if (!is_valid_addr (file)) {
		syscall_exit (-1);
	}

	lock_acquire (&filesys_lock);
	bool success = filesys_create (file, initial_size);
	lock_release (&filesys_lock);

	return success;
}

static bool 
syscall_remove (const char *file) {
	if (!is_valid_addr (file)) {
		syscall_exit (-1);
	}

	lock_acquire (&filesys_lock);
	bool success = filesys_remove (file);
	lock_release (&filesys_lock);

	return success;
}

static int 
syscall_open (const char *file) {
	// Check argument validity
	if (!is_valid_addr (file)) {
		syscall_exit (-1);
	}
	lock_acquire (&filesys_lock);

	// Open file named 'file'
	struct file *f = filesys_open (file);
	if (!f) {
		lock_release (&filesys_lock);
		return -1;
	}
	
	// Allocate empty FD to given file
	int fd = allocate_fd (f);
	if (fd == -1) {
		file_close (f);
	}

	lock_release (&filesys_lock);
	return fd;
}

static int 
syscall_filesize (int fd) {
	if (!is_valid_fd (fd)) {
		return -1;
	}

	struct thread *curr = thread_current ();
	int file_size = -1;
	
	struct file *f = find_file_by_fd (fd);
	// If fd is opened in FD table, get file size
	if (f) {
		file_size = file_length (f); 
	}

	return file_size;
}


static int 
syscall_read (int fd, void *buffer, unsigned size) {
	// Check argument validity
	if (!is_valid_addr (buffer) || !is_valid_addr (buffer + size)) {
		syscall_exit (-1);
	}
	if (!is_writable_page (buffer)) {
		syscall_exit (-1);
	}
	if (!is_valid_fd (fd)) {
		return -1;
	}
	if (size == 0) {
		return 0;
	}

	// If fd is for STDIN, read from user keyboard input
	int read_bytes;
	if (fd == 0) {
		lock_acquire (&filesys_lock);
		for (int i = 0; i < size; i++) {
			uint8_t c = input_getc();
			((char *)buffer)[i] = c;
			if (c == '\n') {
				read_bytes = i;
				break;
			}
		}
		lock_release (&filesys_lock);
		return read_bytes;
	}
	// Else, read from file
	struct file *f = find_file_by_fd (fd);
	if (!f) {
		return -1;
	}

	lock_acquire (&filesys_lock);
	read_bytes = file_read (f, buffer, size);
	lock_release (&filesys_lock);
	return read_bytes;
}

static int 
syscall_write (int fd, void *buffer, unsigned size) {
	if (!is_valid_addr (buffer) || !is_valid_addr (buffer + size)) {
		syscall_exit (-1);
	}
	// if (!is_writable_page (buffer)) {
	// 	printf("(test)not writable!\n");
	// 	syscall_exit (-1);
	// }
	if (!is_valid_fd (fd)) {
		return -1;
	}
	// If given fd is for STDOUT
	if (fd == 1) {
		lock_acquire (&filesys_lock);
		putbuf (buffer, size);
		lock_release (&filesys_lock);
		return size;
	}

	struct thread *curr = thread_current ();
	struct file *f = find_file_by_fd (fd);
	if (!f) {
		return -1;
	}
	lock_acquire (&filesys_lock);
	int written_bytes = file_write (f, buffer, size);
	lock_release (&filesys_lock);
	return written_bytes;
}

static void 
syscall_seek (int fd, unsigned pos) {
	if (!is_valid_fd (fd)) {
		return;
	}
	struct file *f = find_file_by_fd (fd);
	if (!f) {
		return;
	}

	file_seek (f, pos);
}

static unsigned 
syscall_tell (int fd) {
	if (!is_valid_fd (fd)) {
		return;
	}
	struct file *f = find_file_by_fd (fd);
	if (!f) {
		return;
	}

	return file_tell (f);
}

void 
syscall_close (int fd) {
	if (!is_valid_fd (fd)) {
		return;
	}

	struct thread *curr = thread_current ();
	struct file *f = find_file_by_fd (fd);
	if (!f) {
		return;
	}

	lock_acquire (&filesys_lock);

	// Close opened file
	curr->fd_table[fd] = NULL;	// Close for fd table
	file_close (f);				// Close for filesystem

	lock_release (&filesys_lock);

	return;
}

static void *
syscall_mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	// Pre-check if args are valid
	if (!addr 											// If addr is NULL, mmap must fail
	|| is_kernel_vaddr (addr)							// If addr is in kernel area, mmap must fail 
	|| (int64_t) length <= 0 							// If given length is below 0, mmap must fail
	|| fd < 2 || fd >= MAX_FD							// Check fd, excluding STD I/O or other bad fds
	|| addr != pg_round_down (addr) 					// If addr is not page-aligned, mmap must fail
	|| spt_find_page (&thread_current ()->spt, addr)	// If given addr is already mapped, mmap must fail
	|| offset % PGSIZE != 0) {							// If offset is not multiple of PGSIZE, mmap must fail
		return NULL;
	}
	struct file *f = find_file_by_fd (fd);
	// If opened file is NULL or has a length of zero bytes, mmap must fail
	if (!f || file_length (f) == 0) {
		return NULL;
	}

	// File must be independent from original
	f = file_reopen (f);

	// Now it's safe, do mmap and return
	return do_mmap (addr, length, writable, f, offset);
}

static void 
syscall_munmap (void *addr) {
	struct thread *curr = thread_current ();
	struct page *p = spt_find_page (&curr->spt, addr);
	if (!p							// There must be mapped page at addr
	|| addr != p->mmap_start_addr	// Given addr must be start of mmaped page
	|| curr != p->mmap_caller) {	// Current process must be the caller of mmap
		return;
	}		
	// Now it's safe, do munmap
	do_munmap (addr);
}

// Validates given virtual address 
// 'Valid address' means 1. not NULL 2. located in user area 3. mapped properly
// From project3 onwards, valid addr might not be in pml4 due to lazy loading
static bool
is_valid_addr (void *addr) {
	bool cond1 = addr != NULL;
	bool cond2 = is_user_vaddr (addr);
	bool cond3 = spt_find_page (&thread_current ()->spt, addr) ? true : false;

	return addr != NULL 
	&& is_user_vaddr (addr)
	// && pml4_get_page(thread_current ()->pml4, addr) != NULL;}
	&& spt_find_page (&thread_current ()->spt, addr);
}

// Check if page of given addr is writable
// In kernel mode, writing on R/O page do not make PF, so pre-check for buffer is needed
static bool
is_writable_page (void *addr) {
	struct page *page = spt_find_page (&thread_current ()->spt, addr);
	if (!page) {
		return false;
	}
	return page->is_writable;
}

// Check if given fd is in proper boundary
// STD I/O (fd=0,1) must be handled by caller
static bool 
is_valid_fd (int fd) {
	return (0 <= fd && fd < MAX_FD);
}

// Allocate empty FD of current process to given file
static int
allocate_fd (struct file *file) {
	struct thread *curr = thread_current ();
	int fd = -1;
	// Search empty FD
	for (int i = 2; i < MAX_FD; i++) {
		if (curr->fd_table[i] == NULL) {	// If Empty, allocate file to the FD
			curr->fd_table[i] = file;
			fd = i;
			break;
		}
	}
	return fd;
}

// Given fd, return file if fd is opened in current process' fd table
struct file *
find_file_by_fd (int fd) {
	struct thread *curr = thread_current ();
	if (fd <= 1 || fd >= MAX_FD) {
		return NULL;
	}
	return curr->fd_table[fd];
}