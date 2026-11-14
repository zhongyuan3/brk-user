#include <errno.h>
#include <string.h>
#include <sys/resource.h>
#include <syscall.h>

#define NS_PER_SEC 1000000000ULL

static void ns_to_timeval(unsigned long long ns, struct timeval *tv)
{
	tv->tv_sec = (__kernel_time_t)(ns / NS_PER_SEC);
	tv->tv_usec = (__kernel_suseconds_t)((ns % NS_PER_SEC) / 1000ULL);
}

int getrusage(int who, struct rusage *usage)
{
	struct brk_times times;
	long ret;

	if (!usage) {
		errno = EFAULT;
		return -1;
	}

	ret = syscall(SYS_get_times, &times);
	if (ret < 0)
		return -1;

	memset(usage, 0, sizeof(*usage));
	switch (who) {
	case RUSAGE_SELF:
		ns_to_timeval(times.user_ns, &usage->ru_utime);
		ns_to_timeval(times.system_ns, &usage->ru_stime);
		break;
	case RUSAGE_CHILDREN:
		ns_to_timeval(times.child_user_ns, &usage->ru_utime);
		ns_to_timeval(times.child_system_ns, &usage->ru_stime);
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	return 0;
}
