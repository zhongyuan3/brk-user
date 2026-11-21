#ifndef _NET_IF_H
#define _NET_IF_H

#include <sys/socket.h>
#include <sys/types.h>
#include <uapi/net.h>

struct ifreq {
	char ifr_name[IFNAMSIZ];
	union {
		struct sockaddr ifru_addr;
		struct sockaddr ifru_netmask;
		struct sockaddr ifru_broadaddr;
		struct sockaddr ifru_hwaddr;
		short ifru_flags;
		int ifru_mtu;
	} ifr_ifru;
};

#define ifr_addr ifr_ifru.ifru_addr
#define ifr_netmask ifr_ifru.ifru_netmask
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_hwaddr ifr_ifru.ifru_hwaddr
#define ifr_flags ifr_ifru.ifru_flags
#define ifr_mtu ifr_ifru.ifru_mtu

struct ifconf {
	int ifc_len;
	union {
		char *ifcu_buf;
		struct ifreq *ifcu_req;
	} ifc_ifcu;
};

#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

struct ifstats {
	char ifs_name[IFNAMSIZ];
	unsigned long long rx_packets;
	unsigned long long tx_packets;
	unsigned long long rx_bytes;
	unsigned long long tx_bytes;
	unsigned long long rx_errors;
	unsigned long long tx_errors;
	unsigned long long rx_dropped;
	unsigned long long tx_dropped;
};

#endif
