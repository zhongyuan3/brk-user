#include <syscall.h>
#include <uapi/proc.h>
#include <unistd.h>

pid_t getppid(void)
{
	struct __brk_proc proc;

	if (syscall(SYS_GET_PROC, &proc) < 0)
		return (pid_t)-1;
	return proc.ppid;
}
