/* vm.c: Generic interface for virtual memory objects. */
#include <hash.h>
#include <stdbool.h>

#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/process.h"

// Privately added
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
void hash_action_destroy_page (struct hash_elem *e, void *aux);

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		// Create struct page
		struct page *p = malloc (sizeof (struct page));
		if (!p) {
			return false;
		}

		// Select proper initializer with page type
		int type_no_marker = VM_TYPE (type);
		bool (*initializer) (struct page *, enum vm_type, void *);
		switch (type_no_marker) {
			case VM_ANON:
				initializer = anon_initializer;
				break;
			case VM_FILE:
				initializer = file_backed_initializer;
				break;
			case VM_PAGE_CACHE:
				PANIC ("type : VM_PAGE_CACHE\n");
				break;
			default:
				PANIC ("not defined page type : %i\n", type_no_marker);
				break;
		}

		// Call uninit_new, creating "uninit" page struct and initialize it 
		uninit_new (p, upage, init, type, aux, initializer);

		// Modify fields of page, if needed
		p->is_writable = writable; 

		/* TODO: Insert the page into the spt. */
		if (!spt_insert_page (spt, p)) {
			free (p);
			return false;
		}	
	}

	// All tasks done, return true
	return true;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page page_dummy;
	// Round down VA for it pointing start of page
	page_dummy.va = pg_round_down (va);
	struct hash_elem *h_e = hash_find (&spt->hash_ptes, &page_dummy.h_elem);	// Get needed hash_elem from dummy
	if (h_e) {
		return hash_entry (h_e, struct page, h_elem);							// Return needed page from hash_elem
	}

	return NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	bool succ = false;
	// Insert PAGE into hash table SPT->HASH_PTES
	// If we get NULL from hash_insert, it implies 'successful'
	if (!hash_insert (&spt->hash_ptes, &page->h_elem)) {		
		succ = true;
	}
	
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	/* TODO: Fill this function. */
	void *kva = palloc_get_page (PAL_USER | PAL_ZERO);
	if (!kva) {
		PANIC ("todo");
	}
	struct frame *frame = (struct frame *) malloc (sizeof (struct frame));
	if (!frame) {
		palloc_free_page (kva);
		PANIC ("todo");
	}
	frame->kva = kva;


	// ASSERT (frame != NULL);
	// ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {
	// Align addr to page start and find page
	void *page_start = pg_round_down (addr);
	struct page *page = spt_find_page (&thread_current ()->spt, page_start);

	// From 'page', allocate and claim all needed page immediately
	while (page == NULL){
		vm_alloc_page (VM_ANON|VM_MARKER_STACK, page_start, true);
		vm_claim_page (page_start);

		// Advance to next page
		page_start += PGSIZE;
		page = spt_find_page (&thread_current ()->spt, page_start);
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr, bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = spt_find_page (spt, addr);
    uintptr_t addr_int = (uintptr_t)addr;  // 포인터를 정수로 변환

	// printf("handling addr : %p, %i below user stack\n", addr, USER_STACK - addr_int);
	// printf("not_present : %s\n", not_present ? "true" : "false");
	// printf("write : %s\n", write ? "true" : "false");
	// printf("user : %s\n", user ? "true" : "false");

	// If page is not found, there might be a chance that expanding stack can handle the fault.
	if (!page) {
		// printf("page not found\n");
		// Expanding stack must be occured when 'WRITING'
		if (write) {
			// If fault address is in stack boundary, expand stack
			if (USER_STACK - (1 << 20) <= addr && addr <= USER_STACK) {
				vm_stack_growth (addr);
				// printf ("stack grow\n");
				return true;
			}
		}
		// printf("return false\n");
		return false;
	}
	
	bool page_is_writable = page->is_writable;
	bool page_for_spv_only = page->spv_only;
	bool is_stack_page = (page->operations->type && VM_MARKER_STACK);

	// bool not_present;  /* True: not-present page, false: writing r/o page. */
	// bool write;        /* True: access was write, false: access was read. */
	// bool user;         /* True: access by user, false: access by kernel. */

	// If trying to write r/o page, it is true fault
	if (!not_present) {
		return false;
	}

	// If we are trying to write but page is not writable, it is true fault
	if (write && !page_is_writable) {
		return false;
	}

	// If accessed by user but page is for spv only, it is true fault
	if (user && page_for_spv_only) {
		return false;
	}

	// Else, allocate frame to page
	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	// struct page *page = NULL;
	/* TODO: Fill this function */
	// Find page with given va
	struct page *page = spt_find_page (&thread_current ()->spt, va);	
	if (!page) {
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	// Check if page is already in pml4 of current process
	struct thread *curr = thread_current ();
	if (pml4_get_page (curr->pml4, page->va)) {
		return false;
	}

	// Note : vm_get_frame always returns valid, checking *frame is unnecessary
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page (curr->pml4, page->va, frame->kva, page->is_writable)) {
		palloc_free_page (frame->kva);	// Free frame, for preventing memory leak
		free (frame);
		return false;					// If set_page fails, return false
	}
	return swap_in (page, frame->kva);	// Else, swap_in FRAME mapped to PAGE
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init (&spt->hash_ptes, &page_hash, &page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst, struct supplemental_page_table *src) {
	// Note : This function is called by forked process' context
	// Init hash iterator for src spt
	struct hash_iterator h_i;
	hash_first (&h_i, &src->hash_ptes);

	// Traverse src spt, find src page and copy
	while (hash_next (&h_i)) {					
		struct page *src_p = hash_entry (hash_cur (&h_i), struct page, h_elem);		// Get page using iterator
		enum vm_type type = src_p->operations->type;								
		vm_initializer *init = NULL;
		void *aux = NULL;
		bool need_claim = (type != VM_UNINIT);	// If page is already init'd, need claim

		// If page is uninitialized(has no frame), fetch init data before allocating page
		if (type == VM_UNINIT) {
			type = src_p->uninit.type;
			init = src_p->uninit.init;
			// Note : 'aux' is memory allocated struct that can be freed, so malloc & copy all contents
			aux = (struct lazy_load_info *)malloc (sizeof (struct lazy_load_info));
			memcpy (aux, src_p->uninit.aux, sizeof(struct lazy_load_info));
		}

		// Allocate page
		if (!vm_alloc_page_with_initializer (type, src_p->va, src_p->is_writable, init, aux)) {
			continue;
		}

		// Do claim, if necessary
		if (need_claim) {
			if (!vm_claim_page (src_p->va)) {
				continue;
			}
			struct page *dst_p = spt_find_page (dst, src_p->va);
			memcpy (dst_p->frame->kva, src_p->frame->kva, PGSIZE);
		}
	}
	
	// All copy tasks done, return true
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear (&spt->hash_ptes, &hash_action_destroy_page);
}

void
hash_action_destroy_page (struct hash_elem *e, void *aux) {
    struct page* page = hash_entry (e, struct page, h_elem);
    if (page) {
		vm_dealloc_page (page);
	}
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, h_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, h_elem);
  const struct page *b = hash_entry (b_, struct page, h_elem);

  return a->va < b->va;
}