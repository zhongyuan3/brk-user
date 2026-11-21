#include <sys/syscall.h>
#include <unistd.h>

int fsync(int fd)
{
	return syscall(SYS_SYNC_FD, fd);
}
