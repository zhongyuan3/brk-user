#ifndef _NET_ROUTE_H
#define _NET_ROUTE_H

#include <sys/socket.h>
#include <uapi/net.h>

struct rtentry {
	struct sockaddr rt_dst;
	struct sockaddr rt_gateway;
	struct sockaddr rt_genmask;
	unsigned short rt_flags;
	unsigned short rt_metric;
	char rt_dev[IFNAMSIZ];
};

#endif
