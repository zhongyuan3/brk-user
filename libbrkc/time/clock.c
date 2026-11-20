#include <sys/times.h>
#include <time.h>

/*
 * ISO C clock(): processor time used by the program.  The kernel
 * reports user+system time through times(); the child fields are
 * excluded here, matching the "process" (not "children") semantics.
 */
clock_t clock(void)
{
	struct tms buf;

	if (times(&buf) == (clock_t)-1)
		return (clock_t)-1;
	return buf.tms_utime + buf.tms_stime;
}
