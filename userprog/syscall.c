#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <console.h>
#include "devices/input.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "userprog/gdt.h"
#include "userprog/task.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "intrinsic.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
static void syscall_exit (int status);
static void syscall_halt (void);
static int syscall_fork (const char *thread_name, struct intr_frame *if_);
static int syscall_exec (const char *cmd_line);
static int syscall_wait (pid_t pid);
static bool syscall_create (const char *file, unsigned initial_size);
static bool syscall_remove (const char *file);
static int syscall_open (const char *file);
static int syscall_filesize (int fd);
static int syscall_read (int fd, void *buffer, unsigned size);
static int syscall_write (int fd, void *buffer, unsigned size);
static void syscall_seek (int fd, unsigned pos);
static unsigned syscall_tell (int fd);
static void syscall_close (int fd); 
static int syscall_dup2 (int oldfd, int newfd);
static int64_t get_user (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static int allocate_fd (void);

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
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
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
			f->R.rax = syscall_dup2 (f->R.rdi, f->R.rsi);
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
syscall_halt () {
	power_off ();
}

static void
syscall_exit (int status) {
	task_exit (status);
}

static int
syscall_fork (const char *thread_name, struct intr_frame *if_) {
	return process_fork (thread_name, if_);
}

static int
syscall_exec (const char *cmd_line) {
	struct task *task = task_find_by_tid (thread_tid ());
	char* fn_copy;
	if (task == NULL) {
		return -1;
	}

	if (get_user (cmd_line) == -1) {
		task_exit (-1);
	}

	file_close (task->executable);
	task->executable = NULL;
	fn_copy = palloc_get_page (0);
	if (fn_copy == NULL) {
		task_exit (-1);
	}

	strlcpy (fn_copy, cmd_line, PGSIZE);

	if (process_exec (fn_copy) < 0) {
		task_exit (-1);
	}

	NOT_REACHED ();
	return 0;
}

static int
syscall_wait (pid_t pid) {
	return process_wait (pid);
}

static bool 
syscall_create (const char *file, unsigned initial_size) {
	struct task *task = task_find_by_tid (thread_tid ());
	if (task == NULL) {
		return -1;
	}

	if (get_user (file) == -1) {
		task_exit (-1);
	}

	lock_acquire (&process_filesys_lock);
	bool success = filesys_create (file, initial_size);
	lock_release (&process_filesys_lock);

	return success;
}

static bool 
syscall_remove (const char *file) {
	struct task *task = task_find_by_tid (thread_tid ());
	if (task == NULL) {
		return -1;
	}

	if (get_user (file) == -1) {
		task_exit (-1);
	}

	lock_acquire (&process_filesys_lock);
	bool success = filesys_remove (file);
	lock_release (&process_filesys_lock);
	return success;
}

static int 
syscall_open (const char *file) {
	struct task *task = task_find_by_tid (thread_tid ());
	if (task == NULL) {
		return -1;
	}

	if (get_user (file) == -1) {
		task_exit (-1);
	}

	int fd = allocate_fd ();
	if (fd < 0) {
		return -1;
	}

	lock_acquire (&process_filesys_lock);
	struct file *f = filesys_open (file);
	lock_release (&process_filesys_lock);
	if (f == NULL) {
		return -1;
	}

	task->fds[fd].closed = false;
	task->fds[fd].file = f;
	task->fds[fd].fd = fd;
	task->fds[fd].duplicated = false;
	task->fds[fd].dup_count = 0;
	return fd;
}

static int 
syscall_filesize (int fd) {
	struct task *task = task_find_by_tid (thread_tid ());
	struct fd *fd_info = NULL;
	if (task == NULL) {
		return -1;
	}

	fd = task_find_fd_map (task, fd);

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
	struct task *task = task_find_by_tid (thread_tid ());
	if (task == NULL) {
		return -1;
	}
	
	fd = task_find_fd_map (task, fd);

	if (fd < 0 || fd >= MAX_FD) {
		return -1;
	}

	if (get_user (buffer) == -1 || get_user (buffer + size) == -1) {
		task_exit (-1);
	}

	if (task->fds[fd].stdio == 0) {
		for (size_t i = 0; i < size; i++) {
			bool result = put_user (buffer + i, input_getc());
			if (!result) {
				task_exit (-1);
			}
		}

		return size;
	}

	if (task->fds[fd].closed || task->fds[fd].file == NULL) {
		return -1;
	}
	
	lock_acquire (&process_filesys_lock);
	off_t ret = file_read (task->fds[fd].file, buffer, size);
	lock_release (&process_filesys_lock);
	return ret;
}

static int
syscall_write (int fd, void *buffer, unsigned size) {
	struct task *task = task_find_by_tid (thread_tid ());
	if (task == NULL) {
		return -1;
	}

	if (get_user (buffer) == -1 || get_user (buffer + size) == -1) {
		task_exit (-1);
	}

	fd = task_find_fd_map (task, fd);

	if (fd < 0 || fd >= MAX_FD) {
		return -1;
	}

	if (task->fds[fd].stdio == 1) {
		putbuf(buffer, size);
		return size;
	}

	if (task->fds[fd].closed || task->fds[fd].file == NULL) {
		return -1;
	}

	lock_acquire (&process_filesys_lock);
	off_t ret = file_write (task->fds[fd].file, buffer, size);
	lock_release (&process_filesys_lock);
	return ret;
}

static void
syscall_seek (int fd, unsigned pos) {
	struct task *task = task_find_by_tid (thread_tid ());
	if (task == NULL) {
		return;
	}

	fd = task_find_fd_map (task, fd);

	if (fd < 0 || fd >= MAX_FD) {
		return;
	}

	if (task->fds[fd].closed || task->fds[fd].file == NULL) {
		return;
	}
	lock_acquire (&process_filesys_lock);
	file_seek (task->fds[fd].file, pos);
	lock_release (&process_filesys_lock);
}

static unsigned
syscall_tell (int fd) {
	struct task *task = task_find_by_tid (thread_tid ());
	if (task == NULL) {
		return -1;
	}

	fd = task_find_fd_map (task, fd);

	if (fd < 0 || fd >= MAX_FD) {
		return -1;
	}

	if (task->fds[fd].closed || task->fds[fd].file == NULL) {
		return -1;
	}
	lock_acquire (&process_filesys_lock);
	off_t ret = file_tell (task->fds[fd].file);
	lock_release (&process_filesys_lock);
	return ret;
}

static void
syscall_close (int fd) {
	struct task *task = task_find_by_tid (thread_tid ());
	if (task == NULL) {
		return;
	}

	fd = task_find_fd_map (task, fd);

	if (fd < 0 || fd >= MAX_FD) {
		return;
	}
	
	if (task->fds[fd].closed) {
		return;
	}

	/* Neither duplicated nor duplicated by another FD. */
	if (!task->fds[fd].duplicated && task->fds[fd].dup_count == 0) {
		lock_acquire (&process_filesys_lock);
		file_close (task->fds[fd].file);
		lock_release (&process_filesys_lock);
		fd_init (&task->fds[fd], fd);
		return;
	}

	/* Duplicated */
	fd_t parent_fd = task_find_original_fd (task, task->fds[fd].fd);
	if (task->fds[fd].duplicated) {
		fd_init (&task->fds[fd], fd);

		if (parent_fd != fd) {
			task->fds[parent_fd].dup_count -= 1;
		}
		return;
	}

	/* lend, not duplicated */
	if (task->fds[fd].dup_count > 0) {
		task->fds[fd].closed = true;
		task_inherit_fd (task, fd);
		fd_init (&task->fds[fd], fd);
	}
}

static int
syscall_dup2 (int oldfd, int newfd) {
	struct task *task = task_find_by_tid (thread_tid ());
	int newfd_copy = newfd;

	if (task == NULL) {
		return -1;
	}
	
	oldfd = task_find_fd_map (task, oldfd);

	if (newfd >= MAX_FD) {
		newfd = task_find_fd_map (task, newfd);
		if (newfd == -1 && (newfd = allocate_fd ()) == -1) {
			return -1;
		}
	}

	if (oldfd < 0 || oldfd >= MAX_FD) {
		return -1;
	}

	if (newfd < 0 || newfd >= MAX_FD) {
		return -1;
	}

	if (oldfd == newfd) {
		return newfd_copy;
	}

	if (task->fds[oldfd].closed) {
		return -1;
	}

	if (!task->fds[newfd].closed) {
		syscall_close (task->fds[newfd].fd_map);
	}
	
	fd_t parent_fd = task_find_original_fd (task, oldfd);
	if (parent_fd == -1) {
		return -1;
	}

	task->fds[newfd].fd = parent_fd;
	task->fds[newfd].file = task->fds[parent_fd].file;
	task->fds[parent_fd].dup_count += 1;
	task->fds[newfd].duplicated = true;
	task->fds[newfd].closed = false;
	task->fds[newfd].fd_map = newfd_copy;
	task->fds[newfd].dup_count = 0;
	task->fds[newfd].stdio = task->fds[parent_fd].stdio;
	return newfd_copy;
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

static int
allocate_fd (void) {
	struct task *task = task_find_by_tid (thread_tid ());
	
	int fd = -1;
	for (int i = 3; i <= MAX_FD; i++) {
		if (task->fds[i].closed && task->fds[i].dup_count == 0) {
			fd = i;
			break;
		}
	}

	return fd;
}