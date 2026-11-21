#include <syscall.h>
#include <unistd.h>

int pipe2(int pipefd[2], int flags)
{
	return syscall(SYS_PIPE, pipefd, flags);
}
