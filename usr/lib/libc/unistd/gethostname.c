#include <errno.h>
#include <string.h>
#include <sys/utsname.h>

/*
 * The kernel does not track a host name yet, so the value comes from
 * uname()'s nodename (currently fixed).  The copy is truncated like
 * POSIX requires and a too-small buffer fails without touching it.
 */
int gethostname(char *name, size_t len)
{
	struct utsname u;

	if (!name) {
		errno = EFAULT;
		return -1;
	}
	if (!len) {
		errno = EINVAL;
		return -1;
	}
	if (uname(&u) < 0)
		return -1;
	if (strlen(u.nodename) >= len) {
		errno = ENAMETOOLONG;
		return -1;
	}
	strcpy(name, u.nodename);
	return 0;
}
