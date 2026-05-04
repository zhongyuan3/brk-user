#include <syscall.h>
#include <sys/stat.h>

int fstat(int fd, struct stat *buf)
{
	return syscall(SYS_fstat, fd, buf);
}
