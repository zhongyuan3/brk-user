#include <syscall.h>
#include <sys/time.h>

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	return syscall(SYS_gettimeofday, tv, tz);
}
