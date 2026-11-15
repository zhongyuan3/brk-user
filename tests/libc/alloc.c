/*
 * Allocator tests for libbrkc.
 *
 * These tests exercise malloc/calloc/realloc/free directly through the libc
 * implementation, including alignment, coalescing, large regions and the
 * allocator lock under concurrent use.
 */

#include <pthread.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_COUNT 512
#define THREAD_COUNT 4
#define THREAD_ITERS 128

static int fail(const char *test, const char *msg)
{
	fprintf(stderr, "alloc: %s: %s\n", test, msg);
	return -1;
}

static unsigned char pattern_byte(size_t index, unsigned int seed)
{
	return (unsigned char)((index * 37u + seed * 17u + 0x5a) & 0xffu);
}

static void fill_pattern(unsigned char *p, size_t len, unsigned int seed)
{
	for (size_t i = 0; i < len; i++)
		p[i] = pattern_byte(i, seed);
}

static int pattern_matches(const unsigned char *p, size_t len,
			   unsigned int seed)
{
	for (size_t i = 0; i < len; i++) {
		if (p[i] != pattern_byte(i, seed))
			return 0;
	}
	return 1;
}

static int check_ptr(const char *test, const void *p)
{
	if (!p)
		return fail(test, "allocation returned NULL");
	if ((uintptr_t)p % alignof(max_align_t) != 0)
		return fail(test, "misaligned allocation");
	return 0;
}

static int test_small_allocations(void)
{
	for (size_t size = 1; size <= 512; size++) {
		unsigned char *p = malloc(size);

		if (check_ptr("small", p))
			return -1;
		fill_pattern(p, size, (unsigned int)size);
		if (!pattern_matches(p, size, (unsigned int)size))
			return fail("small", "data changed while allocated");
		free(p);
	}

	/* malloc(0) may return NULL or a unique pointer; both are safe to free. */
	free(malloc(0));
	free(NULL);
	return 0;
}

static int test_calloc(void)
{
	size_t nmemb = 257;
	size_t size = 33;
	unsigned char *p = calloc(nmemb, size);

	if (check_ptr("calloc", p))
		return -1;
	for (size_t i = 0; i < nmemb * size; i++) {
		if (p[i] != 0)
			return fail("calloc", "memory is not zeroed");
	}
	free(p);

	volatile size_t half = SIZE_MAX / 2 + 1;
	if (calloc(half, 2) != NULL)
		return fail("calloc", "size overflow did not fail");
	return 0;
}

static int test_realloc(void)
{
	unsigned char *p = malloc(96);
	unsigned char *q;

	if (check_ptr("realloc", p))
		return -1;
	fill_pattern(p, 96, 1);

	q = realloc(p, 64 * 1024);
	if (check_ptr("realloc", q))
		return -1;
	if (!pattern_matches(q, 96, 1))
		return fail("realloc", "grow lost existing data");

	fill_pattern(q, 64 * 1024, 2);
	q = realloc(q, 128);
	if (check_ptr("realloc", q))
		return -1;
	if (!pattern_matches(q, 128, 2))
		return fail("realloc", "shrink lost existing data");
	free(q);

	q = realloc(NULL, 300);
	if (check_ptr("realloc", q))
		return -1;
	fill_pattern(q, 300, 3);
	if (!pattern_matches(q, 300, 3))
		return fail("realloc",
			    "realloc(NULL, size) returned bad memory");
	if (realloc(q, 0) != NULL)
		return fail("realloc",
			    "realloc(ptr, 0) did not free the block");

	volatile size_t impossible = SIZE_MAX;
	p = malloc(64);
	if (check_ptr("realloc", p))
		return -1;
	fill_pattern(p, 64, 4);
	q = realloc(p, impossible);
	if (q != NULL)
		return fail("realloc", "impossible size did not fail");
	if (!pattern_matches(p, 64, 4))
		return fail("realloc", "failed grow modified the old block");
	free(p);
	return 0;
}

static int test_large_allocations(void)
{
	static const size_t sizes[] = {
		64 * 1024 - 1, 64 * 1024,	64 * 1024 + 1,
		1024 * 1024,   2 * 1024 * 1024,
	};

	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		size_t size = sizes[i];
		unsigned char *p = malloc(size);

		if (check_ptr("large", p))
			return -1;
		for (size_t off = 0; off < size; off += 4096)
			p[off] = pattern_byte(off, (unsigned int)i);
		p[size - 1] = pattern_byte(size - 1, (unsigned int)i);
		for (size_t off = 0; off < size; off += 4096) {
			if (p[off] != pattern_byte(off, (unsigned int)i))
				return fail("large", "data mismatch");
		}
		if (p[size - 1] != pattern_byte(size - 1, (unsigned int)i))
			return fail("large", "last byte mismatch");
		free(p);
	}
	return 0;
}

static int test_many_blocks(void)
{
	void *blocks[BLOCK_COUNT];
	size_t sizes[BLOCK_COUNT];

	for (unsigned int cycle = 0; cycle < 3; cycle++) {
		for (size_t i = 0; i < BLOCK_COUNT; i++) {
			sizes[i] = 1 + ((i * 97 + cycle * 31) % 8192);
			blocks[i] = malloc(sizes[i]);
			if (check_ptr("blocks", blocks[i]))
				return -1;
			fill_pattern(blocks[i], sizes[i],
				     (unsigned int)(cycle * 1000 + i));
		}
		for (size_t i = 0; i < BLOCK_COUNT; i++) {
			if (!pattern_matches(blocks[i], sizes[i],
					     (unsigned int)(cycle * 1000 + i)))
				return fail("blocks", "data mismatch");
		}

		/* Free odd blocks first so coalescing has to merge neighbours. */
		for (size_t i = 1; i < BLOCK_COUNT; i += 2)
			free(blocks[i]);
		for (size_t i = 0; i < BLOCK_COUNT; i += 2)
			free(blocks[i]);
	}
	return 0;
}

static int test_limits(void)
{
	volatile size_t impossible = SIZE_MAX;

	if (malloc(impossible) != NULL)
		return fail("limits", "malloc(SIZE_MAX) did not fail");
	if (malloc(impossible - 4096) != NULL)
		return fail("limits", "oversized malloc did not fail");
	return 0;
}

struct thread_worker {
	unsigned int id;
	int failed;
};

static void *thread_main(void *arg)
{
	struct thread_worker *worker = arg;

	for (size_t i = 0; i < THREAD_ITERS; i++) {
		size_t size = 1 + ((worker->id * 97 + i * 53) % 4096);
		unsigned int seed = worker->id * 10000 + (unsigned int)i;
		unsigned char *p = malloc(size);

		if (!p || (uintptr_t)p % alignof(max_align_t) != 0) {
			worker->failed = 1;
			free(p);
			return NULL;
		}
		fill_pattern(p, size, seed);
		if (!pattern_matches(p, size, seed)) {
			worker->failed = 1;
			free(p);
			return NULL;
		}

		if ((i & 7) == 0) {
			unsigned char *q = realloc(p, size + 512);

			if (!q || (uintptr_t)q % alignof(max_align_t) != 0 ||
			    !pattern_matches(q, size, seed)) {
				worker->failed = 1;
				free(q ? q : p);
				return NULL;
			}
			p = q;
			fill_pattern(p + size, 512, seed + 1);
		}
		free(p);
	}
	return NULL;
}

static int test_threads(void)
{
	pthread_t threads[THREAD_COUNT];
	struct thread_worker workers[THREAD_COUNT];

	for (size_t i = 0; i < THREAD_COUNT; i++) {
		workers[i].id = (unsigned int)i;
		workers[i].failed = 0;
		int err = pthread_create(&threads[i], NULL, thread_main,
					 &workers[i]);

		if (err) {
			fprintf(stderr, "alloc: threads: pthread_create: %d\n",
				err);
			return -1;
		}
	}
	for (size_t i = 0; i < THREAD_COUNT; i++) {
		int err = pthread_join(threads[i], NULL);

		if (err) {
			fprintf(stderr, "alloc: threads: pthread_join: %d\n",
				err);
			return -1;
		}
		if (workers[i].failed)
			return fail("threads",
				    "worker allocation check failed");
	}
	return 0;
}

int main(void)
{
	if (test_small_allocations() || test_calloc() || test_realloc() ||
	    test_large_allocations() || test_many_blocks() || test_limits() ||
	    test_threads())
		return 1;

	puts("alloc: ok");
	return 0;
}
