#include <syscall.h>
#include <unistd.h>

int pipe(int pipefd[2])
{
	return pipe2(pipefd, 0);
}
