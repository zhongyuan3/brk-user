#include "pthread_impl.h"

#include <errno.h>
#include <sys/mman.h>

int pthread_join(pthread_t thread, void **retval)
{
	int tid;

	if (!thread || !thread->stack)
		return EINVAL;
	if (thread == __pthread_self_internal())
		return EDEADLK;
	if (thread->joined)
		return EINVAL;

	thread->joined = 1;

	while ((tid = __atomic_load_n(&thread->tid, __ATOMIC_ACQUIRE)) != 0) {
		long err = __pthread_futex_wait(&thread->tid, tid, NULL);

		if (err == -1 && errno != EAGAIN && errno != EINTR)
			return errno;
	}

	__sync_synchronize();
	if (retval)
		*retval = thread->retval;

	void *stack = thread->stack;
	size_t stack_size = thread->stack_size;

	thread->stack = NULL;
	munmap(stack, stack_size);
	return 0;
}
