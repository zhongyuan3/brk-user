#include <sys/select.h>
#include <syscall.h>

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	   struct timeval *timeout)
{
	return (int)syscall(SYS_select, nfds, readfds, writefds, exceptfds,
			    timeout);
}
