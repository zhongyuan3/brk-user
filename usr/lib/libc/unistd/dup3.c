#include <syscall.h>
#include <unistd.h>

int dup3(int oldfd, int newfd, int flags)
{
	return syscall(SYS_DUP_TO, oldfd, newfd, flags);
}
