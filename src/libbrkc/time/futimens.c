#include <fcntl.h>
#include <sys/stat.h>
#include <syscall.h>

int futimens(int fd, const struct timespec times[2])
{
	return syscall(SYS_futimens, fd, times);
}
