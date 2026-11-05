#include <syscall.h>
#include <time.h>
#include <unistd.h>

unsigned int sleep(unsigned int seconds)
{
	struct timespec duration;
	duration.tv_sec = seconds;
	duration.tv_nsec = 0;
	struct timespec rem;
	if (nanosleep(&duration, &rem))
		return rem.tv_sec;
	return 0;
}
