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

	if (syscall(SYS_sigsuspend, mask, &old) != -1 || errno != EINTR)
		return -1;

	/*
	 * The kernel leaves the temporary mask installed so that the handler
	 * runs with the mask passed to sigsuspend().  Restore the caller's
	 * mask now that the handler has returned.
	 */
	(void)sigprocmask(SIG_SETMASK, &old, NULL);
	errno = EINTR;
	return -1;
}
