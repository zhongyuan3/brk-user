#include <syscall.h>
#include <unistd.h>

void _exit(int status)
{
	/*
	 * POSIX _exit() terminates the process.  SYS_exit in BRK only exits the
	 * calling thread; SYS_exit_group is the process-wide primitive.
	 */
	syscall(SYS_exit_group, status);
	for (;;)
		;
}
