#include <stddef.h>
#include <sys/wait.h>
#include <syscall.h>

pid_t wait(int *wstatus)
{
	return wait4(-1, wstatus, 0, NULL);
}
