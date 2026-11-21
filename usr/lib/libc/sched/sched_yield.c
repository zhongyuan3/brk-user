#include <sched.h>
#include <sys/syscall.h>

int sched_yield(void)
{
	return syscall(SYS_YIELD);
}
