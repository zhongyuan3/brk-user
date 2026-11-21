#ifndef _POLL_H
#define _POLL_H

#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include <uapi/poll.h>

struct pollfd {
	int fd;
	short events;
	short revents;
};

typedef __brk_nfds_t nfds_t;

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
	  const sigset_t *sigmask);

#endif
