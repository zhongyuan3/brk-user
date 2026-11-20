#include <errno.h>
#include <signal.h>
#include <stddef.h>

int sigwait(const sigset_t *set, int *sig)
{
	siginfo_t info;
	int ret = sigtimedwait(set, NULL, &info);

	if (ret < 0)
		return errno;
	if (sig)
		*sig = ret;
	return 0;
}
