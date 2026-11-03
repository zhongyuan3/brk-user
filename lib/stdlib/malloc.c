#include <brk/bits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/*
 * Two-Level Segregated Fit (TLSF) allocator.
 *
 * Blocks live in one or more page-aligned mmap() regions.  Every region has
 * a small metadata header and two sentinel blocks, so physical coalescing
 * never crosses a region boundary.  A region whose blocks are all free is
 * returned to the kernel with munmap().
 *
 * Every block starts with a header and stores the size of its previous
 * physical block so free() can coalesce in both directions.  Free blocks are
 * indexed by a two-level bitmap:
 *
 *   fl = floor(log2(size))
 *   sl = (size >> (fl - 5)) & 31
 *
 * Allocation is O(1) in the number of blocks; finding the owning region on
 * free is linear in the number of mapped regions.
 */

#define TLSF_ALIGN alignof(max_align_t)
#define TLSF_SL_LOG2 5
#define TLSF_SL_COUNT (1u << TLSF_SL_LOG2)

#if __SIZEOF_SIZE_T__ == 8
#define TLSF_FL_INDEX_MAX 38
#else
#define TLSF_FL_INDEX_MAX 30
#endif

#define TLSF_FL_INDEX_COUNT (TLSF_FL_INDEX_MAX + 1)
#define TLSF_SMALL_BLOCK_SIZE ((size_t)1 << TLSF_SL_LOG2)
#define TLSF_MAX_BLOCK ((size_t)1 << (TLSF_FL_INDEX_MAX + 1))
#define TLSF_PAGE_SIZE ((size_t)4096)
#define TLSF_REGION_MIN ((size_t)64 * 1024)
#define TLSF_REGION_CACHE_MAX 4
#define TLSF_REGION_CACHE_BYTES ((size_t)4 * 1024 * 1024)
#define TLSF_USED_BIT ((size_t)1)

struct tlsf_block {
	size_t size; /* total block size, low bit marks an allocated block */
	size_t prev_size; /* total size of the previous physical block */
};

struct tlsf_free_links {
	struct tlsf_block *next;
	struct tlsf_block *prev;
};

struct tlsf_region {
	struct tlsf_region *next;
	char *base;
	size_t size;
	struct tlsf_block *first;
	struct tlsf_block *end;
	size_t usable_size;
};

#define TLSF_HEADER_SIZE \
	((sizeof(struct tlsf_block) + TLSF_ALIGN - 1) & ~(TLSF_ALIGN - 1))
#define TLSF_MIN_BLOCK_SIZE                                                \
	((TLSF_HEADER_SIZE + sizeof(struct tlsf_free_links) + TLSF_ALIGN - \
	  1) &                                                             \
	 ~(TLSF_ALIGN - 1))
#define TLSF_REGION_META_SIZE \
	((sizeof(struct tlsf_region) + TLSF_ALIGN - 1) & ~(TLSF_ALIGN - 1))
#define TLSF_REGION_OVERHEAD (TLSF_REGION_META_SIZE + 2 * TLSF_HEADER_SIZE)

_Static_assert(TLSF_ALIGN >= sizeof(void *), "TLSF alignment too small");
_Static_assert(TLSF_FL_INDEX_MAX < 64, "TLSF class count too large");
_Static_assert(TLSF_PAGE_SIZE % TLSF_ALIGN == 0,
	       "TLSF page size must be block aligned");
_Static_assert(TLSF_MIN_BLOCK_SIZE >=
		       TLSF_HEADER_SIZE + sizeof(struct tlsf_free_links),
	       "TLSF minimum block too small");
_Static_assert(TLSF_REGION_META_SIZE % TLSF_ALIGN == 0,
	       "TLSF region metadata must be block aligned");
_Static_assert(TLSF_REGION_MIN >= TLSF_REGION_OVERHEAD + TLSF_MIN_BLOCK_SIZE,
	       "TLSF minimum region too small");

static struct tlsf_block *tlsf_free_list[TLSF_FL_INDEX_COUNT][TLSF_SL_COUNT];
static uint32_t tlsf_sl_bitmap[TLSF_FL_INDEX_COUNT];
static uint64_t tlsf_fl_bitmap;
static struct tlsf_region *tlsf_regions;
static struct tlsf_region *tlsf_region_cache;
static size_t tlsf_region_cache_count;
static size_t tlsf_region_cache_bytes;

static int tlsf_fls(size_t size)
{
	return (int)(sizeof(uint64_t) * 8 - 1 -
		     brk_clz64((uint64_t)size));
}

static int tlsf_ffs64(uint64_t word)
{
	return brk_ctz64(word);
}

static int tlsf_mapping_insert(size_t size, int *fl, int *sl)
{
	int f;

	if (size < TLSF_SMALL_BLOCK_SIZE) {
		*fl = 0;
		*sl = (int)(size / TLSF_ALIGN);
		return *sl < (int)TLSF_SL_COUNT ? 0 : -1;
	}

	f = tlsf_fls(size);
	if (f > TLSF_FL_INDEX_MAX)
		return -1;

	*fl = f;
	*sl = (int)((size >> (f - TLSF_SL_LOG2)) & (TLSF_SL_COUNT - 1));
	return 0;
}

static int tlsf_mapping_search(size_t size, int *fl, int *sl)
{
	size_t round;
	int f;

	if (size < TLSF_SMALL_BLOCK_SIZE)
		return tlsf_mapping_insert(size, fl, sl);

	f = tlsf_fls(size);
	if (f > TLSF_FL_INDEX_MAX)
		return -1;

	round = ((size_t)1 << (f - TLSF_SL_LOG2)) - 1;
	if (size > (size_t)-1 - round)
		return -1;
	return tlsf_mapping_insert(size + round, fl, sl);
}

static size_t tlsf_class_min(int fl, int sl)
{
	if (fl == 0)
		return (size_t)sl * TLSF_ALIGN;
	return ((size_t)1 << fl) + ((size_t)sl << (fl - TLSF_SL_LOG2));
}

static size_t tlsf_block_size(const struct tlsf_block *block)
{
	return block->size & ~TLSF_USED_BIT;
}

static int tlsf_block_is_free(const struct tlsf_block *block)
{
	return !(block->size & TLSF_USED_BIT);
}

static void *tlsf_block_payload(struct tlsf_block *block)
{
	return (char *)block + TLSF_HEADER_SIZE;
}

static struct tlsf_block *tlsf_payload_block(void *ptr)
{
	return (struct tlsf_block *)((char *)ptr - TLSF_HEADER_SIZE);
}

static struct tlsf_free_links *tlsf_block_links(struct tlsf_block *block)
{
	return (struct tlsf_free_links *)((char *)block + TLSF_HEADER_SIZE);
}

static void tlsf_free_insert(struct tlsf_block *block)
{
	struct tlsf_free_links *links = tlsf_block_links(block);
	int fl, sl;

	if (tlsf_mapping_insert(tlsf_block_size(block), &fl, &sl))
		return;

	links->prev = NULL;
	links->next = tlsf_free_list[fl][sl];
	if (links->next)
		tlsf_block_links(links->next)->prev = block;
	tlsf_free_list[fl][sl] = block;

	tlsf_sl_bitmap[fl] |= (uint32_t)1 << sl;
	tlsf_fl_bitmap |= (uint64_t)1 << fl;
}

static void tlsf_free_remove(struct tlsf_block *block)
{
	struct tlsf_free_links *links = tlsf_block_links(block);
	int fl, sl;

	if (tlsf_mapping_insert(tlsf_block_size(block), &fl, &sl))
		return;

	if (links->prev)
		tlsf_block_links(links->prev)->next = links->next;
	else
		tlsf_free_list[fl][sl] = links->next;
	if (links->next)
		tlsf_block_links(links->next)->prev = links->prev;

	if (!tlsf_free_list[fl][sl]) {
		tlsf_sl_bitmap[fl] &= ~((uint32_t)1 << sl);
		if (!tlsf_sl_bitmap[fl])
			tlsf_fl_bitmap &= ~((uint64_t)1 << fl);
	}
}

static int tlsf_find(size_t size, struct tlsf_block **out)
{
	uint64_t sl_map;
	uint64_t fl_map;
	int fl, sl;

	if (!tlsf_fl_bitmap)
		return 0;
	if (tlsf_mapping_search(size, &fl, &sl))
		return 0;

	sl_map = (uint64_t)tlsf_sl_bitmap[fl] & (~(uint64_t)0 << sl);
	if (!sl_map) {
		if (fl + 1 >= 64)
			return 0;
		fl_map = tlsf_fl_bitmap & (~(uint64_t)0 << (fl + 1));
		if (!fl_map)
			return 0;
		fl = tlsf_ffs64(fl_map);
		sl = tlsf_ffs64((uint64_t)tlsf_sl_bitmap[fl]);
	} else {
		sl = tlsf_ffs64(sl_map);
	}

	*out = tlsf_free_list[fl][sl];
	return *out != NULL;
}

static int tlsf_size_to_block(size_t size, size_t *need)
{
	size_t aligned;

	if (size == 0)
		return -1;
	if (size > (size_t)-1 - (TLSF_ALIGN - 1))
		return -1;

	aligned = (size + TLSF_ALIGN - 1) & ~(TLSF_ALIGN - 1);
	if (aligned > (size_t)-1 - TLSF_HEADER_SIZE)
		return -1;

	*need = aligned + TLSF_HEADER_SIZE;
	if (*need < TLSF_MIN_BLOCK_SIZE)
		*need = TLSF_MIN_BLOCK_SIZE;
	if (*need >= TLSF_MAX_BLOCK)
		return -1;
	return 0;
}

static struct tlsf_region *tlsf_region_find(const void *ptr)
{
	uintptr_t addr = (uintptr_t)ptr;
	struct tlsf_region *region;

	for (region = tlsf_regions; region; region = region->next) {
		uintptr_t base = (uintptr_t)region->base;

		if (addr >= base && addr < base + region->size)
			return region;
	}
	return NULL;
}

static void tlsf_region_remove(struct tlsf_region *region)
{
	struct tlsf_region **link = &tlsf_regions;

	while (*link && *link != region)
		link = &(*link)->next;
	if (*link)
		*link = region->next;
}

static void tlsf_region_activate(struct tlsf_region *region, char *base,
				 size_t size)
{
	struct tlsf_block *start;
	struct tlsf_block *first;
	struct tlsf_block *end;
	size_t usable;

	start = (struct tlsf_block *)(base + TLSF_REGION_META_SIZE);
	first = (struct tlsf_block *)((char *)start + TLSF_HEADER_SIZE);
	end = (struct tlsf_block *)(base + size - TLSF_HEADER_SIZE);
	usable = (size_t)((char *)end - (char *)first);

	start->size = TLSF_HEADER_SIZE | TLSF_USED_BIT;
	start->prev_size = 0;

	first->size = usable;
	first->prev_size = TLSF_HEADER_SIZE;

	end->size = TLSF_HEADER_SIZE | TLSF_USED_BIT;
	end->prev_size = usable;

	region->next = tlsf_regions;
	region->base = base;
	region->size = size;
	region->first = first;
	region->end = end;
	region->usable_size = usable;
	tlsf_regions = region;

	tlsf_free_insert(first);
}

static void tlsf_region_cache_flush(void)
{
	while (tlsf_region_cache) {
		struct tlsf_region *region = tlsf_region_cache;

		tlsf_region_cache = region->next;
		(void)munmap(region->base, region->size);
	}
	tlsf_region_cache_count = 0;
	tlsf_region_cache_bytes = 0;
}

static void tlsf_region_put_cache(struct tlsf_region *region)
{
	if (tlsf_region_cache_count >= TLSF_REGION_CACHE_MAX ||
	    region->size > TLSF_REGION_CACHE_BYTES - tlsf_region_cache_bytes) {
		(void)munmap(region->base, region->size);
		return;
	}

	region->next = tlsf_region_cache;
	tlsf_region_cache = region;
	tlsf_region_cache_count++;
	tlsf_region_cache_bytes += region->size;
}

static struct tlsf_region *tlsf_region_take_cache(size_t min_usable)
{
	struct tlsf_region **link = &tlsf_region_cache;
	struct tlsf_region **best_link = NULL;
	size_t best_size = 0;
	int found = 0;

	for (; *link; link = &(*link)->next) {
		if ((*link)->usable_size < min_usable)
			continue;
		if (!found || (*link)->usable_size < best_size) {
			best_link = link;
			best_size = (*link)->usable_size;
			found = 1;
		}
	}
	if (!best_link)
		return NULL;

	struct tlsf_region *region = *best_link;

	*best_link = region->next;
	region->next = NULL;
	tlsf_region_cache_count--;
	tlsf_region_cache_bytes -= region->size;
	return region;
}

static int tlsf_grow(size_t min_size)
{
	struct tlsf_region *region;
	char *base;
	size_t class_min;
	size_t size;
	size_t usable;
	int fl, sl;

	if (tlsf_mapping_search(min_size, &fl, &sl))
		return -1;
	class_min = tlsf_class_min(fl, sl);
	if (class_min < min_size ||
	    class_min > TLSF_MAX_BLOCK - TLSF_REGION_OVERHEAD)
		return -1;

	region = tlsf_region_take_cache(class_min);
	if (region) {
		tlsf_region_activate(region, region->base, region->size);
		return 0;
	}

	size = class_min + TLSF_REGION_OVERHEAD;
	if (size < TLSF_REGION_MIN)
		size = TLSF_REGION_MIN;
	if (size > TLSF_MAX_BLOCK)
		size = TLSF_MAX_BLOCK;
	size = (size + TLSF_PAGE_SIZE - 1) & ~(TLSF_PAGE_SIZE - 1);
	if (size < class_min + TLSF_REGION_OVERHEAD || size > TLSF_MAX_BLOCK)
		return -1;

	base = mmap(NULL, size, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED) {
		tlsf_region_cache_flush();
		base = mmap(NULL, size, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}
	if (base == MAP_FAILED)
		return -1;
	if (((uintptr_t)base & (TLSF_ALIGN - 1)) != 0) {
		(void)munmap(base, size);
		return -1;
	}

	region = (struct tlsf_region *)base;
	usable = size - TLSF_REGION_OVERHEAD;
	if (usable < class_min || usable >= TLSF_MAX_BLOCK) {
		(void)munmap(base, size);
		return -1;
	}

	tlsf_region_activate(region, base, size);
	return 0;
}

static void tlsf_split(struct tlsf_block *block, size_t need)
{
	struct tlsf_block *tail;
	struct tlsf_block *next;
	size_t size = tlsf_block_size(block);
	size_t rest;

	if (size < need + TLSF_MIN_BLOCK_SIZE)
		return;

	rest = size - need;
	block->size = need | (block->size & TLSF_USED_BIT);

	tail = (struct tlsf_block *)((char *)block + need);
	tail->size = rest;
	tail->prev_size = need;

	next = (struct tlsf_block *)((char *)tail + rest);
	next->prev_size = rest;

	/* The split tail may be adjacent to a free block. */
	if (tlsf_block_is_free(next)) {
		struct tlsf_block *after;

		tlsf_free_remove(next);
		tail->size = rest + tlsf_block_size(next);
		after = (struct tlsf_block *)((char *)next +
					      tlsf_block_size(next));
		after->prev_size = tlsf_block_size(tail);
	}

	tlsf_free_insert(tail);
}

static void tlsf_free_block(struct tlsf_block *block)
{
	struct tlsf_block *next;
	struct tlsf_block *prev;
	struct tlsf_region *region;

	block->size &= ~TLSF_USED_BIT;

	next = (struct tlsf_block *)((char *)block + tlsf_block_size(block));
	if (tlsf_block_is_free(next)) {
		tlsf_free_remove(next);
		block->size = tlsf_block_size(block) + tlsf_block_size(next);
	}

	prev = (struct tlsf_block *)((char *)block - block->prev_size);
	if (tlsf_block_is_free(prev)) {
		tlsf_free_remove(prev);
		prev->size = tlsf_block_size(prev) + tlsf_block_size(block);
		block = prev;
	}

	next = (struct tlsf_block *)((char *)block + tlsf_block_size(block));
	next->prev_size = tlsf_block_size(block);

	region = tlsf_region_find(block);
	if (region && block == region->first &&
	    tlsf_block_size(block) == region->usable_size) {
		tlsf_region_remove(region);
		tlsf_region_put_cache(region);
		return;
	}

	tlsf_free_insert(block);
}

void *malloc(size_t size)
{
	struct tlsf_block *block;
	size_t need;

	if (tlsf_size_to_block(size, &need))
		return NULL;

	if (!tlsf_find(need, &block)) {
		if (tlsf_grow(need) || !tlsf_find(need, &block))
			return NULL;
	}

	tlsf_free_remove(block);
	tlsf_split(block, need);
	block->size |= TLSF_USED_BIT;
	return tlsf_block_payload(block);
}

void *calloc(size_t nmemb, size_t size)
{
	void *ptr;
	size_t total;

	if (nmemb == 0 || size == 0)
		return NULL;
	if (nmemb > (size_t)-1 / size)
		return NULL;

	total = nmemb * size;
	ptr = malloc(total);
	if (ptr)
		memset(ptr, 0, total);
	return ptr;
}

void *realloc(void *ptr, size_t size)
{
	struct tlsf_block *block;
	struct tlsf_block *next;
	void *new_ptr;
	size_t capacity;
	size_t copy;
	size_t need;
	size_t total;

	if (!ptr)
		return malloc(size);
	if (size == 0) {
		free(ptr);
		return NULL;
	}
	if (tlsf_size_to_block(size, &need))
		return NULL;

	block = tlsf_payload_block(ptr);
	if (need <= tlsf_block_size(block)) {
		tlsf_split(block, need);
		return ptr;
	}

	next = (struct tlsf_block *)((char *)block + tlsf_block_size(block));
	if (tlsf_block_is_free(next)) {
		total = tlsf_block_size(block);
		if (tlsf_block_size(next) <= (size_t)-1 - total)
			total += tlsf_block_size(next);
		else
			total = 0;

		if (total >= need) {
			struct tlsf_block *after;

			tlsf_free_remove(next);
			block->size = total | TLSF_USED_BIT;
			after = (struct tlsf_block *)((char *)block + total);
			after->prev_size = total;
			tlsf_split(block, need);
			return ptr;
		}
	}

	new_ptr = malloc(size);
	if (!new_ptr)
		return NULL;

	capacity = tlsf_block_size(block) - TLSF_HEADER_SIZE;
	copy = capacity < size ? capacity : size;
	memcpy(new_ptr, ptr, copy);
	free(ptr);
	return new_ptr;
}

void free(void *ptr)
{
	struct tlsf_block *block;

	if (!ptr)
		return;

	block = tlsf_payload_block(ptr);
	if (tlsf_block_is_free(block))
		return;

	tlsf_free_block(block);
}
