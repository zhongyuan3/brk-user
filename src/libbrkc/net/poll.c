#include <poll.h>
#include <syscall.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	return (int)syscall(SYS_poll, fds, nfds, timeout);
}
