#ifndef _POLL_H
#define _POLL_H

#include <sys/types.h>
#include <uapi/poll.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout);

#endif
