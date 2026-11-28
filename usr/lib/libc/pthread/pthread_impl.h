#ifndef _PTHREAD_IMPL_H
#define _PTHREAD_IMPL_H

#include <pthread.h>
#include <stdint.h>
#include <syscall.h>
#include <time.h>
#include <uapi/futex.h>

struct pthread {
	void *(*start)(void *);
	void *arg;
	void *retval;
	void *stack;
	size_t stack_size;
	int tid;
	int joined;
	int detached;
	int exiting;
	int detach_queued;
	struct pthread *detach_next;
	int err;
};

struct pthread *__pthread_self_internal(void);
int __pthread_reaper_start(void);
void __pthread_detach_cleanup(struct pthread *self);

static inline uintptr_t __pthread_get_tp(void)
{
	uintptr_t tp;

	__asm__ volatile("mv %0, tp" : "=r"(tp));
	return tp;
}

static inline long __pthread_futex_wait(int *uaddr, int val,
					const struct timespec *timeout)
{
	return syscall(SYS_FUTEX, uaddr, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, val,
		       timeout, 0, 0);
}

static inline long __pthread_futex_wake(int *uaddr, int nr)
{
	return syscall(SYS_FUTEX, uaddr, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, nr, 0,
		       0, 0);
}

#endif
