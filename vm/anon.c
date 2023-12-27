/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/vaddr.h"		// Privately added
#include "lib/kernel/bitmap.h"	// Privately added

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

#define SEC_PER_PG = (PGSIZE / DISK_SECTOR_SIZE);

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */

	// Init frame_list, set clock pointer
	list_init (&frame_list);
	clock_pointer = list_head (&frame_list);	// Initial clock pointer

	// Get swap disk(channel 1, dev 1)
	swap_disk = disk_get (1, 1);

	// Calc nums of disk sector(512B), and convert it into page size(4096B)
	// int num_sector_swap_disk = disk_size (swap_disk) - disk_size (swap_disk) % 8;	// In case disk_size is not multiple of 8
	int num_page_swap_disk = disk_size (swap_disk) * DISK_SECTOR_SIZE / PGSIZE;

	// Create bitmap representing swap_disk
	bm_swap_disk = malloc (sizeof (struct bitmap));
	bm_swap_disk = bitmap_create (num_page_swap_disk);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;

}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

	// Read-in from disk to memory, using saved idx
	disk_sector_t idx_target = anon_page->idx_swap_out;
	void *kva_target = kva;
	for (int i = 0; i < 8; i++) {
		printf("(test)swapin! idx target : %i, kva target : %p\n", idx_target, kva_target);
		disk_read (swap_disk, idx_target, kva);
		idx_target++;
		kva_target += DISK_SECTOR_SIZE;
	}

	// Set bit of the idx to false, indicating that the sector is now usable for other pages
	size_t idx_page = anon_page->idx_swap_out / 8;
	bitmap_set_multiple (bm_swap_disk, idx_page, 1, false);

	// Tasks done, return true
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	// Find empty idx in swap_disk
	size_t idx_page = bitmap_scan_and_flip (bm_swap_disk, 0, 1, false);
	printf("(test)idxpage : %i\n", idx_page);
	disk_sector_t idx_sector = idx_page * 8;

	// Write data to the given idx
	disk_sector_t idx_target = idx_sector;
	void *kva_target = page->frame->kva;
	for (int i = 0; i < 8; i++) {
		printf("(test)swapout! idx target : %i, kva target : %p\n", idx_target, kva_target);
		disk_write (swap_disk, idx_target, kva_target);
		idx_target++;
		kva_target += DISK_SECTOR_SIZE;
	}

	// Save idx for future swap-in
	anon_page->idx_swap_out = idx_sector;		

	// Tasks done, return true
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
