#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
// Process-related syscalls
static void syscall_halt (void);
void syscall_exit (int status);
static tid_t syscall_fork (const char *thread_name, struct intr_frame *f);
static int syscall_exec (const char *cmd_line);
static int syscall_wait (tid_t tid);
// File-related syscalls
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned pos);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd);
// Functions for implementing syscalls
static bool is_valid_address (void *addr);
static int allocate_fd (struct file *file);

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
		// case SYS_FORK:
		// 	f->R.rax = syscall_fork (f->R.rdi, f);
		// 	break;
		case SYS_EXEC:
			f->R.rax = syscall_exec (f->R.rdi);
			break;
		// case SYS_WAIT:
		// 	f->R.rax = syscall_wait (f->R.rdi);
		// 	break;
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
		// case SYS_MMAP:
		// case SYS_MUNMAP:
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
	printf("%s: exit(%d)\n", curr->name, status);
	thread_exit ();
}

// static tid_t syscall_fork (const char *thread_name, struct intr_frame *f);

static int 
syscall_exec (const char *cmd_line) {
	if (!is_valid_address (cmd_line)) {
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

// static int syscall_wait (tid_t tid);

static bool 
syscall_create (const char *file, unsigned initial_size) {
	if (!is_valid_address (file)) {
		syscall_exit (-1);
	}

	lock_acquire (&filesys_lock);
	bool success = filesys_create (file, initial_size);
	lock_release (&filesys_lock);

	return success;
}

static bool 
syscall_remove (const char *file) {
	if (!is_valid_address (file)) {
		syscall_exit (-1);
	}

	lock_acquire (&filesys_lock);
	bool success = filesys_remove (file);
	lock_release (&filesys_lock);

	return success;
}

static int 
syscall_open (const char *file) {
	if (!is_valid_address (file)) {
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
		file_close (file);
	}
	// // If opened running file, deny write
	// if (strcmp (thread_name (), file) == 0) {
	// 	file_deny_write (f);
	// }

	lock_release (&filesys_lock);
	return fd;
}

static int 
syscall_filesize (int fd) {
	struct thread *curr = thread_current ();
	struct file *f;
	int file_size = -1;

	// Check if fd is in proper boundary
	if (fd < 0 || fd >= MAX_FD) {
		return -1;
	}
	// Set file_size, if fd is opened in FD table
	if (curr->fdt[fd].in_use) {
		f = curr->fdt[fd].file;
		file_size = file_length (f); 
	}

	return file_size;
}


static int 
syscall_read (int fd, void *buffer, unsigned size) {
	// Check argument validity
	if (!is_valid_address (buffer)) {
		syscall_exit (-1);
	}
	if (fd < 0 || fd >= MAX_FD) {
		return -1;
	}
	if (!thread_current ()->fdt[fd].in_use) {
		return -1;
	}
	if (size == 0) {
		return 0;
	}

	// If fd is for STDIN, read from user keyboard input
	int read_bytes;
	if (fd == 0) {
		for (int i = 0; i < size; i++) {
			uint8_t c = input_getc();
			((char *)buffer)[i] = c;
			if (c == '\n') {
				read_bytes = i;
				break;
			}
		}
		return read_bytes;
	}
	// Else, read from file
	struct file *f = thread_current ()->fdt[fd].file;
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
	if (!is_valid_address (buffer)) {
		syscall_exit (-1);
	}
	// If given fd is not in proper boundary
	if (fd <= 0 || fd >= MAX_FD) {
		return -1;
	}

	// If given fd is for STDOUT
	if (fd == 1) {
		putbuf (buffer, size);
		return size;
	}

	struct thread *curr = thread_current ();
	// Check if given FD is in use
	if (!curr->fdt[fd].in_use) {
		return -1;
	}
	lock_acquire (&filesys_lock);
	int written_bytes = file_write (curr->fdt[fd].file, buffer, size);
	lock_release (&filesys_lock);
	return written_bytes;
}

static void 
syscall_seek (int fd, unsigned pos) {
	// Check if given fd is proper
	if (fd < 0 || fd >= MAX_FD) {
		return;
	}
	if (!thread_current ()->fdt[fd].in_use) {
		return;
	}

	struct file *f = thread_current ()->fdt[fd].file;
	file_seek (f, pos);
}

static unsigned 
syscall_tell (int fd) {
	// Check if given fd is proper
	if (fd < 0 || fd >= MAX_FD) {
		return;
	}
	if (!thread_current ()->fdt[fd].in_use) {
		return;
	}

	struct file *f = thread_current ()->fdt[fd].file;
	return file_tell (f);
}

static void 
syscall_close (int fd) {
	// Check if fd is in proper boundary
	if (fd <= 1 || fd >= MAX_FD) {
		return;
	}

	struct thread *curr = thread_current ();
	// Check if fd is not in use
	if (!curr->fdt[fd].in_use) {
		return;
	}

	lock_acquire (&filesys_lock);

	// Close opened file
	file_close (curr->fdt[fd].file);
	curr->fdt[fd].in_use = false;

	lock_release (&filesys_lock);

	return;
}

// Validates given virtual address 
// 'Valid address' means 1. not NULL 2. located in user area 3. mapped properly
static bool
is_valid_address (void *addr) {
	return addr != NULL 
	&& is_user_vaddr (addr) 
	&& pml4_get_page(thread_current ()->pml4, addr) != NULL;
}

// Allocate empty FD of current process to given file
static int
allocate_fd (struct file *file) {
	struct thread *curr = thread_current ();
	int fd = -1;

	// Search empty FD
	for (int i = 2; i < MAX_FD; i++) {
		if (!curr->fdt[i].in_use) {
			curr->fdt[i].in_use = true;
			curr->fdt[i].file = file;
			fd = i;
			break;
		}
	}

	return fd;
}