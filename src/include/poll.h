#ifndef _POLL_H
#define _POLL_H

#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <uapi/poll.h>

typedef __brk_ulong_t nfds_t;

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout,
	  const sigset_t *sigmask);

#endif
