/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
static void file_backed_write_back (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	// Get aux from struct uninit_page
	struct lazy_load_info *aux = page->uninit.aux;

	struct file_page *file_page = &page->file;

	// Move file data, from uninit_page to file_page
	file_page->file = aux->file;
	file_page->ofs = aux->ofs;
	file_page->page_read_bytes = aux->page_read_bytes;
	file_page->page_zero_bytes = aux->page_zero_bytes;

	// End initializing, return true
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	// struct file_page *file_page UNUSED = &page->file;
	file_backed_write_back (page);
}

// Given page, write back all the dirty pages to storage and clear the page
static void 
file_backed_write_back (struct page *page) {
	struct file_page *file_page = &page->file;
	void *va = page->va;

	struct thread *curr = thread_current ();

	// If given page is found to be dirty, write back all the changes to disk
	if (pml4_is_dirty (curr->pml4, va)) {
		int written_bytes = file_write_at (file_page->file, page->frame->kva, file_page->page_read_bytes, file_page->ofs);
		if (written_bytes != file_page->page_read_bytes) {
			printf ("write back failed, need check!\n");
			return;
		}
		pml4_set_dirty (curr->pml4, va, false);
	}

	// Set present bit of page to FALSE, later reference of this page will fault
	pml4_clear_page (curr->pml4, va);
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	// Set read_bytes
	size_t read_bytes = file_length (file) < PGSIZE ? file_length (file) : length;
	// Calc the number of pages needed
	int page_need = (read_bytes + PGSIZE - 1) / PGSIZE;			
	off_t ofs = offset;
	void *upage = addr;

	// Copied from load_segment
	while (read_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		// printf ("(test)mmap upage:%p, file:%p, ofs:%i\n", upage,file,ofs);
		// printf ("(test)read : %i, pread : %i, pzero : %i\n", read_bytes, page_read_bytes, page_zero_bytes);

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		struct lazy_load_info *aux = malloc (sizeof (struct lazy_load_info));
		if (!aux) {
			return NULL;
		}
		aux->file = file;
		aux->ofs = ofs;
		aux->page_read_bytes = page_read_bytes;
		aux->page_zero_bytes = page_zero_bytes;
		// Aux gets additional data for munmap, for setting fields of page
		aux->mmap_start_addr = addr;
		aux->mmap_num_contig_page = page_need;
		aux->mmap_caller = thread_current ();
		

		if (!vm_alloc_page_with_initializer (VM_FILE|VM_MARKER_MMAP, upage, writable, lazy_load_segment, aux)) {
			free (aux);
			return NULL;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		upage += PGSIZE;
		ofs += page_read_bytes;
	}
	// If do_mmap is successful, return start addr
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct page *p = spt_find_page (&thread_current ()->spt, addr);
	struct file *f = p->file.file;
	int total_page = p->mmap_num_contig_page;
	void *addr_temp = addr;

	// Iterate all mmaped page and destory
	for (int i = 0; i < total_page; i++) {
		p = spt_find_page (&thread_current ()->spt, addr_temp);
		if (!p) {
			return;
		}
		// // // Page must be 'destroyed', as it will not be used anymore
		// file_backed_destroy (p);
		// // Remove page from spt, freeing all the resources
		// spt_remove_page (&thread_current ()->spt, p);

		file_backed_write_back (p);
		
		// Advance
		addr_temp += PGSIZE;
	}
	// After all write-back is done, close opened file
	file_close (f);
}