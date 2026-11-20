#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/select.h>
#include <time.h>

static int select_common(int nfds, fd_set *readfds, fd_set *writefds,
			 fd_set *exceptfds, const struct timespec *timeout,
			 const sigset_t *sigmask)
{
	struct pollfd *pfds = NULL;
	unsigned int count = 0;
	int ready = 0;

	if (nfds < 0) {
		errno = EINVAL;
		return -1;
	}

	if (nfds) {
		pfds = malloc((size_t)nfds * sizeof(*pfds));
		if (!pfds)
			return -1;

		for (int fd = 0; fd < nfds; fd++) {
			short events = 0;

			if (readfds && FD_ISSET(fd, readfds))
				events |= POLLIN;
			if (writefds && FD_ISSET(fd, writefds))
				events |= POLLOUT;
			if (exceptfds && FD_ISSET(fd, exceptfds))
				events |= POLLPRI;
			if (!events)
				continue;

			pfds[count].fd = fd;
			pfds[count].events = events;
			pfds[count].revents = 0;
			count++;
		}
	}

	if (readfds)
		FD_ZERO(readfds);
	if (writefds)
		FD_ZERO(writefds);
	if (exceptfds)
		FD_ZERO(exceptfds);

	int ret = ppoll(pfds, count, timeout, sigmask);
	if (ret < 0) {
		free(pfds);
		return -1;
	}

	for (unsigned int i = 0; i < count; i++) {
		short revents = pfds[i].revents;
		int fd = pfds[i].fd;

		if (!revents)
			continue;
		ready++;

		if (readfds && (revents & (POLLIN | POLLHUP | POLLERR)))
			FD_SET(fd, readfds);
		if (writefds && (revents & (POLLOUT | POLLERR)))
			FD_SET(fd, writefds);
		if (exceptfds && (revents & POLLPRI))
			FD_SET(fd, exceptfds);
	}

	free(pfds);
	return ready;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	   struct timeval *timeout)
{
	struct timespec ts;
	const struct timespec *tsp = NULL;

	if (timeout) {
		if (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
		    timeout->tv_usec >= 1000000) {
			errno = EINVAL;
			return -1;
		}
		ts.tv_sec = timeout->tv_sec;
		ts.tv_nsec = timeout->tv_usec * 1000L;
		tsp = &ts;
	}

	return select_common(nfds, readfds, writefds, exceptfds, tsp, NULL);
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	    const struct timespec *timeout, const sigset_t *sigmask)
{
	return select_common(nfds, readfds, writefds, exceptfds, timeout,
			     sigmask);
}
