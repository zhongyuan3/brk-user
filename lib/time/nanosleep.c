#include <syscall.h>
#include <time.h>

int nanosleep(const struct timespec *duration, struct timespec *rem)
{
	return syscall(SYS_nanosleep, duration, rem);
}
