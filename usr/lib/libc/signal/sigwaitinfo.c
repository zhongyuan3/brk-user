#include <signal.h>
#include <stddef.h>

int sigwaitinfo(const sigset_t *set, siginfo_t *info)
{
	return sigtimedwait(set, NULL, info);
}
