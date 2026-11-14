#include <sys/resource.h>
#include <sys/times.h>
#include <syscall.h>
#include <time.h>

#define USER_HZ 1000
#define NS_PER_SEC 1000000000ULL

static clock_t ns_to_ticks(unsigned long long ns)
{
	return (clock_t)(ns / (NS_PER_SEC / USER_HZ));
}

clock_t times(struct tms *buf)
{
	struct brk_times times;
	struct timespec now;

	if (syscall(SYS_get_times, &times) < 0 ||
	    clock_gettime(CLOCK_BOOTTIME, &now) != 0)
		return (clock_t)-1;

	if (buf) {
		buf->tms_utime = ns_to_ticks(times.user_ns);
		buf->tms_stime = ns_to_ticks(times.system_ns);
		buf->tms_cutime = ns_to_ticks(times.child_user_ns);
		buf->tms_cstime = ns_to_ticks(times.child_system_ns);
	}

	return (clock_t)now.tv_sec * USER_HZ +
	       (clock_t)now.tv_nsec / (NS_PER_SEC / USER_HZ);
}
