#include "pthread_impl.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <uapi/proc.h>

#define PTHREAD_STACK_ALIGN 4096UL

static void *pthread_start(void *arg)
{
	struct pthread *self = arg;

	/*
	 * pthread_create() stores the kernel tid after SYS_CREATE_THREAD
	 * returns.  Wait for that handshake so the tid slot cannot be cleared
	 * by a fast-exiting thread before the creator has published it.
	 */
	while (__atomic_load_n(&self->tid, __ATOMIC_ACQUIRE) == 0)
		(void)__pthread_futex_wait(&self->tid, 0, 0);

	void *ret = self->start(self->arg);
	pthread_exit(ret);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
		   void *(*start_routine)(void *), void *arg)
{
	struct pthread *self;
	struct brk_thread_create_args args;
	size_t stack_size;
	size_t total;
	void *region;
	int detachstate;
	long tid;

	if (!thread || !start_routine)
		return EINVAL;

	detachstate = attr ? attr->detachstate : PTHREAD_CREATE_JOINABLE;
	if (detachstate != PTHREAD_CREATE_JOINABLE &&
	    detachstate != PTHREAD_CREATE_DETACHED)
		return EINVAL;
	if (detachstate == PTHREAD_CREATE_DETACHED) {
		int err = __pthread_reaper_start();

		if (err)
			return err;
	}

	stack_size = (attr && attr->stacksize) ? attr->stacksize :
						 PTHREAD_STACK_DEFAULT;
	if (stack_size < PTHREAD_STACK_MIN)
		return EINVAL;
	if (stack_size >
	    (size_t)-1 - sizeof(struct pthread) - (PTHREAD_STACK_ALIGN - 1))
		return EINVAL;

	total = sizeof(struct pthread) + stack_size;
	total = (total + PTHREAD_STACK_ALIGN - 1) & ~(PTHREAD_STACK_ALIGN - 1);

	region = mmap(NULL, total, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (region == MAP_FAILED)
		return ENOMEM;

	self = region;
	memset(self, 0, sizeof(*self));
	self->start = start_routine;
	self->arg = arg;
	self->stack = region;
	self->stack_size = total;
	self->detached = detachstate == PTHREAD_CREATE_DETACHED;

	args.stack = (__brk_ulong_t)((uintptr_t)region + total) & ~0xFUL;
	args.entry = (__brk_ulong_t)pthread_start;
	args.arg = (__brk_ulong_t)self;
	args.tls = (__brk_ulong_t)self;
	args.clear_tid = (__brk_ulong_t)&self->tid;
	args.flags = 0;

	tid = syscall(SYS_CREATE_THREAD, &args);
	if (tid == -1) {
		int err = errno;

		munmap(region, total);
		return err;
	}

	__atomic_store_n(&self->tid, (int)tid, __ATOMIC_RELEASE);
	(void)__pthread_futex_wake(&self->tid, 1);

	*thread = self;
	return 0;
}
