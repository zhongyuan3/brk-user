#include <poll.h>
#include <syscall.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	return (int)syscall(SYS_poll, fds, nfds, timeout);
}

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
	  const sigset_t *sigmask)
{
	return (int)syscall(SYS_ppoll, fds, nfds, timeout, sigmask);
}
