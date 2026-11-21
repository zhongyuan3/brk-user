#include <errno.h>
#include <syscall.h>
#include <time.h>
#include <uapi/sysinfo.h>

int clock_getres(clockid_t clockid, struct timespec *res)
{
	struct __brk_sysinfo info;
	__brk_u64 ns;

	if (syscall(SYS_GET_SYSINFO, &info) < 0)
		return -1;

	switch (clockid) {
	case CLOCK_REALTIME:
	case CLOCK_MONOTONIC:
	case CLOCK_BOOTTIME:
		ns = info.mono_resolution_ns;
		break;
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID:
		ns = info.cpu_resolution_ns;
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	if (res) {
		res->tv_sec = (__brk_time_t)(ns / 1000000000ULL);
		res->tv_nsec = (__brk_long_t)(ns % 1000000000ULL);
	}
	return 0;
}
