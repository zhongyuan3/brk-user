#include <sys/resource.h>
#include <syscall.h>

int getrusage(int who, struct rusage *usage)
{
	return syscall(SYS_getrusage, who, usage);
}
