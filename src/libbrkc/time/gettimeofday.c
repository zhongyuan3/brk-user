#include <sys/time.h>
#include <syscall.h>
#include <uapi/syscall_internal.h>

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	return syscall(SYS_gettimeofday, tv, tz);
}
