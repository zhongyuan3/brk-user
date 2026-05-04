#include <syscall.h>
#include <unistd.h>

void _exit(int status)
{
	syscall(SYS_exit, status);
	for (;;)
		;
}
