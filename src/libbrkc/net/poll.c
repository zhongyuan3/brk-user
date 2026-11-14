#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <syscall.h>
#include <time.h>

int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
	  const sigset_t *sigmask)
{
	return (int)syscall(SYS_ppoll, fds, nfds, timeout, sigmask);
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	struct timespec ts;

	if (timeout < 0)
		return ppoll(fds, nfds, NULL, NULL);

	ts.tv_sec = timeout / 1000;
	ts.tv_nsec = (long)(timeout % 1000) * 1000000L;
	return ppoll(fds, nfds, &ts, NULL);
}
