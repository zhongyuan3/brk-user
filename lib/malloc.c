#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct block {
	struct block *next;
	size_t size;
	bool free;
};

static struct block *head = NULL;

void *malloc(size_t size)
{
	struct block *curr, *prev, *new_blk;
	size_t tot_size;

	if (size == 0)
		return NULL;

	curr = head;
	prev = NULL;

	while (curr) {
		if (curr->free && curr->size >= size) {
			curr->free = false;
			return curr + 1;
		}
		prev = curr;
		curr = curr->next;
	}

	tot_size = sizeof(struct block) + size;
	new_blk = sbrk((intptr_t)tot_size);
	if (new_blk == (void *)-1)
		return NULL;

	new_blk->size = size;
	new_blk->free = false;
	new_blk->next = NULL;

	if (!prev)
		head = new_blk;
	else
		prev->next = new_blk;

	return new_blk + 1;
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
	size_t copy;

	if (!ptr)
		return malloc(size);
	if (size == 0) {
		free(ptr);
		return NULL;
	}

	blk = ((struct block *)ptr) - 1;
	new_ptr = malloc(size);
	if (!new_ptr)
		return NULL;

	copy = blk->size < size ? blk->size : size;
	memcpy(new_ptr, ptr, copy);
	free(ptr);
	return new_ptr;
}

void free(void *ptr)
{
	struct block *blk, *curr;

	if (!ptr)
		return;

	blk = ((struct block *)ptr) - 1;
	blk->free = true;

	curr = head;
	while (curr && curr->next) {
		if (curr->free && curr->next->free) {
			curr->size += sizeof(struct block) + curr->next->size;
			curr->next = curr->next->next;
		} else {
			curr = curr->next;
		}
	}
}
