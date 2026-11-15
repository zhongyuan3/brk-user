#include <syscall.h>
#include <unistd.h>

void _exit(int status)
{
	/*
	 * POSIX _exit() terminates the process.  SYS_EXIT in BRK only exits the
	 * calling thread; SYS_EXIT_GROUP is the process-wide primitive.
	 */
	syscall(SYS_EXIT_GROUP, status);
	for (;;)
		;
}
