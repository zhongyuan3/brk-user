#include <syscall.h>
#include <sys/times.h>

clock_t times(struct tms *buf)
{
	return syscall(SYS_times, buf);
}
