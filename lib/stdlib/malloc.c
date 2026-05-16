#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ALIGN alignof(max_align_t)

struct block {
	struct block *next;
	size_t size; /* user bytes, multiple of ALIGN */
	bool free;
};

static struct block *head;

static inline size_t round_up(size_t n, size_t al)
{
	return (n + al - 1) & ~(al - 1);
}

#define HDRSZ (round_up(sizeof(struct block), ALIGN))

static inline void *block_payload(struct block *b)
{
	return (char *)b + HDRSZ;
}

static inline struct block *payload_block(void *p)
{
	return (struct block *)((char *)p - HDRSZ);
}

static inline size_t align_size(size_t n)
{
	if (n == 0)
		return 0;
	return round_up(n, ALIGN);
}

/* Smallest worthwhile free tail when splitting (header + tiny user). */
#define MIN_SPLIT_FREE (HDRSZ + ALIGN)

static void coalesce_from(struct block *start)
{
	struct block *b = start;

	while (b && b->next) {
		if (b->free && b->next->free) {
			b->size += HDRSZ + b->next->size;
			b->next = b->next->next;
		} else {
			b = b->next;
		}
	}
}

static void split_block(struct block *b, size_t want_user)
{
	size_t rest;

	if (b->size < want_user + MIN_SPLIT_FREE)
		return;

	rest = b->size - want_user - HDRSZ;
	b->size = want_user;

	struct block *tail =
		(struct block *)((char *)block_payload(b) + want_user);

	tail->size = rest;
	tail->free = true;
	tail->next = b->next;
	b->next = tail;
}

void *malloc(size_t size)
{
	struct block *prev, *b;
	size_t want, need;

	if (size == 0)
		return NULL;

	want = align_size(size);
	if (want < size)
		return NULL;

	need = HDRSZ + want;
	if (need < HDRSZ)
		return NULL;

	prev = NULL;
	for (b = head; b; prev = b, b = b->next) {
		if (!b->free || b->size < want)
			continue;

		b->free = false;
		split_block(b, want);
		coalesce_from(b->next);
		return block_payload(b);
	}

	b = sbrk((intptr_t)need);
	if (b == (void *)-1)
		return NULL;

	b->size = want;
	b->free = false;
	b->next = NULL;

	if (!prev)
		head = b;
	else
		prev->next = b;

	return block_payload(b);
}

void *calloc(size_t nmemb, size_t size)
{
	size_t total;

	if (nmemb == 0 || size == 0)
		return NULL;
	if (nmemb > SIZE_MAX / size)
		return NULL;
	total = nmemb * size;
	void *ptr = malloc(total);
	if (ptr)
		memset(ptr, 0, total);
	return ptr;
}

void *realloc(void *ptr, size_t size)
{
	struct block *blk;
	void *new_ptr;
	size_t copy, want;

	if (!ptr)
		return malloc(size);
	if (size == 0) {
		free(ptr);
		return NULL;
	}

	blk = payload_block(ptr);
	want = align_size(size);
	if (want < size)
		return NULL;

	if (want <= blk->size) {
		if (blk->size >= want + MIN_SPLIT_FREE) {
			split_block(blk, want);
			coalesce_from(blk->next);
		}
		return ptr;
	}

	if (blk->next && blk->next->free) {
		size_t merged = blk->size + HDRSZ + blk->next->size;

		if (merged >= want) {
			struct block *n = blk->next;

			blk->next = n->next;
			blk->size = merged;
			split_block(blk, want);
			coalesce_from(blk->next);
			return ptr;
		}
	}

	new_ptr = malloc(size);
	if (!new_ptr)
		return NULL;

	copy = blk->size < want ? blk->size : want;
	memcpy(new_ptr, ptr, copy);
	free(ptr);
	return new_ptr;
}

void free(void *ptr)
{
	struct block *blk;

	if (!ptr)
		return;

	blk = payload_block(ptr);
	blk->free = true;
	coalesce_from(head);
}
