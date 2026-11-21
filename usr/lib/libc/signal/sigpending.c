#include <signal.h>
#include <syscall.h>

int sigpending(sigset_t *set)
{
	return syscall(SYS_GET_PENDING, set);
}
