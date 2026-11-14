#include <errno.h>
#include <string.h>
#include <sys/utsname.h>
#include <syscall.h>
#include <uapi/sysinfo.h>

int uname(struct utsname *buf)
{
	struct brk_sysinfo info;

	if (!buf) {
		errno = EFAULT;
		return -1;
	}
	if (syscall(SYS_sysinfo, &info) < 0)
		return -1;

	memset(buf, 0, sizeof(*buf));
	strlcpy(buf->sysname, info.sysname, sizeof(buf->sysname));
	strlcpy(buf->nodename, "none", sizeof(buf->nodename));
	strlcpy(buf->release, info.release, sizeof(buf->release));
	strlcpy(buf->version, info.release, sizeof(buf->version));
	strlcpy(buf->machine, info.machine, sizeof(buf->machine));
	strlcpy(buf->domainname, "none", sizeof(buf->domainname));
	return 0;
}
