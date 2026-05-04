#include <syscall.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int *wstatus, int options)
{
	return wait4(pid, wstatus, options, NULL);
}
