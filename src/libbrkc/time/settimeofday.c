#include <sys/time.h>
#include <syscall.h>
#include <uapi/syscall_internal.h>

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
	return syscall(SYS_settimeofday, tv, tz);
}
