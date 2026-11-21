#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <syscall.h>

/*
 * pause() sleeps until any signal is delivered.  Composed from
 * WAIT_SIGNAL's sigsuspend mode: the mask passed in is the
 * caller's own, so the handler runs with the usual mask and the
 * kernel leaves exactly that mask installed - nothing to restore
 * afterwards.
 */
int pause(void)
{
	sigset_t mask;

	if (sigprocmask(SIG_SETMASK, NULL, &mask) != 0)
		return -1;

	if (syscall(SYS_WAIT_SIGNAL, &mask, NULL, NULL, SIG_WAIT_SETMASK) != -1 ||
	    errno != EINTR)
		return -1;

	errno = EINTR;
	return -1;
}
