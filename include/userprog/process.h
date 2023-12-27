#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

// Privately added
struct lazy_load_info {
	struct file *file;
	off_t ofs;
	size_t page_read_bytes;
	size_t page_zero_bytes;
	// Aux data for munmap
	void *mmap_start_addr;			// Start addr of mmaped pages
	int mmap_num_contig_page;		// Number of contiguous mmaped pages
};
#ifdef VM
bool lazy_load_segment (struct page *page, void *aux);
#endif // VM


#endif /* userprog/process.h */
