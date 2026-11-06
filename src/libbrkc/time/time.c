#include <syscall.h>
#include <time.h>

time_t time(time_t *tloc)
{
	return (time_t)syscall(SYS_time, tloc);
}
