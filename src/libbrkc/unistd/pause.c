#include <syscall.h>
#include <unistd.h>

int pause(void)
{
	return syscall(SYS_pause);
}
