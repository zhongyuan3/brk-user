#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <syscall.h>

int sigsuspend(const sigset_t *mask)
{
	sigset_t old;

	if (!mask) {
		errno = EINVAL;
		return -1;
	}

	/*
	 * Fetch the caller's mask first: WAIT_SIGNAL in sigsuspend
	 * mode installs the new mask and leaves it in place while
	 * the handler runs, and it is restored here afterwards.
	 */
	if (sigprocmask(SIG_SETMASK, NULL, &old) != 0)
		return -1;

	if (syscall(SYS_WAIT_SIGNAL, mask, NULL, NULL, SIG_WAIT_SETMASK) != -1 ||
	    errno != EINTR)
		return -1;

	(void)sigprocmask(SIG_SETMASK, &old, NULL);
	errno = EINTR;
	return -1;
}
