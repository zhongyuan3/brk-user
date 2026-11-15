#include <signal.h>
#include <syscall.h>

int sigtimedwait(const sigset_t *set, const struct timespec *timeout,
		 siginfo_t *info)
{
	return (int)syscall(SYS_SIGTIMEDWAIT, set, timeout, info);
}
