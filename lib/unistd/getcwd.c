#include <syscall.h>
#include <unistd.h>

char *getcwd(char *buf, size_t size)
{
	if (syscall(SYS_getcwd, buf, size) < 0)
		return NULL;
	return buf;
}
