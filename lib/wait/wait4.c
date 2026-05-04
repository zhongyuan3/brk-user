#include <syscall.h>
#include <sys/wait.h>

pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage)
{
	return syscall(SYS_wait4, pid, wstatus, options, rusage);
}
