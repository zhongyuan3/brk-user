#include <syscall.h>
#include <sys/utsname.h>

int uname(struct utsname *buf)
{
	return syscall(SYS_uname, buf);
}
