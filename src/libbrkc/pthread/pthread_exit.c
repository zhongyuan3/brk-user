#include "pthread_impl.h"

#include <syscall.h>

void pthread_exit(void *retval)
{
	struct pthread *self = __pthread_self_internal();

	self->retval = retval;
	__sync_synchronize();

	__pthread_detach_cleanup(self);

	/*
	 * SYS_EXIT is thread-only in BRK.  Process-wide exit is _exit(),
	 * which uses SYS_EXIT_GROUP.
	 */
	(void)syscall(SYS_EXIT, 0);
	for (;;)
		;
}
