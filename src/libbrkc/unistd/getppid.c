#include <syscall.h>
#include <uapi/proc.h>
#include <unistd.h>

pid_t getppid(void)
{
	struct brk_proc proc;

	if (syscall(SYS_getproc, &proc) < 0)
		return (pid_t)-1;
	return proc.ppid;
}
