#include "pthread_impl.h"

#include <syscall.h>

void pthread_exit(void *retval)
{
	struct pthread *self = __pthread_self_internal();

	self->retval = retval;
	__sync_synchronize();

	/*
	 * SYS_exit is thread-only in BRK.  Process-wide exit is _exit(),
	 * which uses SYS_exit_group.
	 */
	(void)syscall(SYS_exit, 0);
	for (;;)
		;
}
