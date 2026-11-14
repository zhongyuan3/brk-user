#include <sys/resource.h>
#include <sys/times.h>
#include <syscall.h>
#include <time.h>

#define USER_HZ 1000

static clock_t timeval_to_ticks(const struct timeval *tv)
{
	return (clock_t)tv->tv_sec * USER_HZ +
	       (clock_t)tv->tv_usec / (1000000 / USER_HZ);
}

clock_t times(struct tms *buf)
{
	struct rusage self, children;
	struct timespec now;

	if (getrusage(RUSAGE_SELF, &self) != 0 ||
	    getrusage(RUSAGE_CHILDREN, &children) != 0 ||
	    clock_gettime(CLOCK_BOOTTIME, &now) != 0)
		return (clock_t)-1;

	if (buf) {
		buf->tms_utime = timeval_to_ticks(&self.ru_utime);
		buf->tms_stime = timeval_to_ticks(&self.ru_stime);
		buf->tms_cutime = timeval_to_ticks(&children.ru_utime);
		buf->tms_cstime = timeval_to_ticks(&children.ru_stime);
	}

	return (clock_t)now.tv_sec * USER_HZ +
	       (clock_t)now.tv_nsec / (1000000000 / USER_HZ);
}
