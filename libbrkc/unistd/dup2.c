#include <syscall.h>
#include <unistd.h>

int dup2(int oldfd, int newfd)
{
	if (oldfd == newfd)
		return newfd;
	return dup3(oldfd, newfd, 0);
}
