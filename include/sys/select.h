#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H

#include <sys/time.h>
#include <sys/types.h>
#include <uapi/select.h>

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	   struct timeval *timeout);

#endif
