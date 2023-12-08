#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/process.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "filesys/file.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

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
	// TODO: Your implementation goes here.
	switch (f->R.rax) {
		case SYS_HALT:
		case SYS_EXIT:
		case SYS_FORK:
		case SYS_EXEC:
		case SYS_WAIT:
		case SYS_CREATE:
		case SYS_REMOVE:
		case SYS_OPEN:
		case SYS_FILESIZE:
			break;
		case SYS_READ:
			break;
		case SYS_WRITE:
		case SYS_SEEK:
		case SYS_TELL:
		case SYS_CLOSE:
			PANIC ("Unimplemented syscall syscall_%ld", f->R.rax);
			break;
		case SYS_MMAP:
		case SYS_MUNMAP:
		case SYS_CHDIR:
		case SYS_MKDIR:
		case SYS_READDIR:
		case SYS_ISDIR:
		case SYS_INUMBER:
		case SYS_SYMLINK:
			PANIC ("Unimplemented syscall syscall_%ld", f->R.rax);
			break;
		case SYS_DUP2:
			break;
		case SYS_MOUNT:
		case SYS_UMOUNT:
			PANIC ("Unimplemented syscall syscall_%ld", f->R.rax);
			break;
		default:
			PANIC ("Unkown syscall syscall_%ld", f->R.rax);
	}
	printf("system call!\n");
	thread_exit();
}

int 
syscall_filesize (int fd) {
	struct thread *curr = thread_current ();
	struct task *task = process_find_by_tid (curr->tid);
	struct fd *fd_info = NULL;
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

int 
syscall_read (int fd, void *buffer, unsigned size) {

}
bool 
syscall_create (const char *file, unsigned initial_size) {
	// Creates a new file called file initially initial_size bytes in size. Returns true if successful, false otherwise. 
	// Creating a new file does not open it: opening the new file is a separate operation which would require a open system call.
	return;
}

bool 
syscall_remove (const char *file) {
	return;
}

int 
syscall_open (const char *file) {
	return;
}