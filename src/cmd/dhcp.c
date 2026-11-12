#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC_COOKIE 0x63825363U
#define DHCP_OPTION_END 255
#define DHCP_OPTION_PAD 0
#define DHCP_OPTION_MESSAGE_TYPE 53
#define DHCP_OPTION_SUBNET_MASK 1
#define DHCP_OPTION_ROUTER 3
#define DHCP_OPTION_DNS 6
#define DHCP_OPTION_REQUESTED_IP 50
#define DHCP_OPTION_SERVER_ID 54
#define DHCP_OPTION_PARAMETER_LIST 55

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5
#define DHCP_NAK 6

struct dhcp_packet {
	uint8_t op;
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;
	uint32_t xid;
	uint16_t secs;
	uint16_t flags;
	uint32_t ciaddr;
	uint32_t yiaddr;
	uint32_t siaddr;
	uint32_t giaddr;
	uint8_t chaddr[16];
	uint8_t sname[64];
	uint8_t file[128];
	uint32_t cookie;
	uint8_t options[312];
} __attribute__((packed));

struct dhcp_lease {
	uint8_t type;
	uint32_t xid;
	uint32_t yiaddr;
	uint32_t server_id;
	uint32_t mask;
	uint32_t router;
	uint32_t dns;
};

static int set_ifreq_addr(int fd, const char *name, unsigned long cmd,
			  uint32_t addr)
{
	struct ifreq ifr;
	struct sockaddr_in *sin;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = addr;
	return ioctl(fd, cmd, &ifr);
}

static int set_if_up(int fd, const char *name)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
		return -1;
	ifr.ifr_flags = (short)(ifr.ifr_flags | IFF_UP);
	return ioctl(fd, SIOCSIFFLAGS, &ifr);
}

static int get_mac(int fd, const char *name, uint8_t *mac)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, IFNAMSIZ - 1);
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0)
		return -1;
	memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
	return 0;
}

static void dhcp_put_option(struct dhcp_packet *pkt, size_t *off, uint8_t type,
			    const void *data, size_t len)
{
	if (*off + 2 + len > sizeof(pkt->options))
		return;
	pkt->options[(*off)++] = type;
	pkt->options[(*off)++] = (uint8_t)len;
	if (len)
		memcpy(&pkt->options[*off], data, len);
	*off += len;
}

static size_t dhcp_build(struct dhcp_packet *pkt, uint32_t xid,
			 const uint8_t *mac, uint8_t type, uint32_t requested_ip,
			 uint32_t server_id)
{
	size_t off = 0;
	uint8_t params[] = { DHCP_OPTION_SUBNET_MASK, DHCP_OPTION_ROUTER,
			     DHCP_OPTION_DNS };
	uint8_t msg = type;

	memset(pkt, 0, sizeof(*pkt));
	pkt->op = 1;
	pkt->htype = 1;
	pkt->hlen = 6;
	pkt->xid = htonl(xid);
	pkt->flags = htons(0x8000);
	memcpy(pkt->chaddr, mac, 6);
	pkt->cookie = htonl(DHCP_MAGIC_COOKIE);

	dhcp_put_option(pkt, &off, DHCP_OPTION_MESSAGE_TYPE, &msg, 1);
	dhcp_put_option(pkt, &off, DHCP_OPTION_PARAMETER_LIST, params,
			sizeof(params));
	if (requested_ip)
		dhcp_put_option(pkt, &off, DHCP_OPTION_REQUESTED_IP,
				&requested_ip, sizeof(requested_ip));
	if (server_id)
		dhcp_put_option(pkt, &off, DHCP_OPTION_SERVER_ID, &server_id,
				sizeof(server_id));
	pkt->options[off++] = DHCP_OPTION_END;

	return offsetof(struct dhcp_packet, options) + off;
}

static int dhcp_parse(const void *buf, size_t len, struct dhcp_lease *lease)
{
	const struct dhcp_packet *pkt = buf;
	size_t off = 0;
	size_t opt_max;

	if (len < offsetof(struct dhcp_packet, options) ||
	    pkt->op != 2 || pkt->cookie != htonl(DHCP_MAGIC_COOKIE))
		return -1;

	opt_max = len - offsetof(struct dhcp_packet, options);
	if (opt_max > sizeof(pkt->options))
		opt_max = sizeof(pkt->options);

	memset(lease, 0, sizeof(*lease));
	lease->xid = ntohl(pkt->xid);
	lease->yiaddr = pkt->yiaddr;

	while (off < opt_max) {
		uint8_t type = pkt->options[off];
		uint8_t olen;

		if (type == DHCP_OPTION_PAD) {
			off++;
			continue;
		}
		if (type == DHCP_OPTION_END)
			break;
		if (off + 1 >= opt_max)
			break;
		olen = pkt->options[off + 1];
		if (off + 2 + olen > opt_max)
			break;
		if (type == DHCP_OPTION_MESSAGE_TYPE && olen >= 1)
			lease->type = pkt->options[off + 2];
		else if (type == DHCP_OPTION_SUBNET_MASK && olen >= 4)
			memcpy(&lease->mask, &pkt->options[off + 2], 4);
		else if (type == DHCP_OPTION_ROUTER && olen >= 4)
			memcpy(&lease->router, &pkt->options[off + 2], 4);
		else if (type == DHCP_OPTION_DNS && olen >= 4)
			memcpy(&lease->dns, &pkt->options[off + 2], 4);
		else if (type == DHCP_OPTION_SERVER_ID && olen >= 4)
			memcpy(&lease->server_id, &pkt->options[off + 2], 4);
		off += 2 + olen;
	}
	return lease->type ? 0 : -1;
}

static int dhcp_send(int fd, const struct dhcp_packet *pkt, size_t len)
{
	struct sockaddr_in dst = {
		.sin_family = AF_INET,
		.sin_port = htons(DHCP_SERVER_PORT),
		.sin_addr.s_addr = INADDR_BROADCAST,
	};

	return sendto(fd, pkt, len, 0, (const struct sockaddr *)&dst,
		      sizeof(dst));
}

static int dhcp_wait(int fd, uint32_t xid, uint8_t want,
		     struct dhcp_lease *lease)
{
	struct dhcp_packet pkt;

	for (;;) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		ssize_t n;
		int ret;

		ret = poll(&pfd, 1, 3000);
		if (ret <= 0)
			return -1;
		n = recvfrom(fd, &pkt, sizeof(pkt), 0, NULL, NULL);
		if (n < 0)
			continue;
		if (dhcp_parse(&pkt, (size_t)n, lease) != 0)
			continue;
		if (lease->xid != xid)
			continue;
		if (want && lease->type != want)
			continue;
		return 0;
	}
}

static int configure_interface(int fd, const char *name,
			       const struct dhcp_lease *lease)
{
	struct rtentry rt;
	struct sockaddr_in *sin;
	uint32_t mask = lease->mask ? lease->mask : 0xffffff00U;
	uint32_t broadcast = lease->yiaddr | ~mask;

	if (set_ifreq_addr(fd, name, SIOCSIFADDR, lease->yiaddr) != 0 ||
	    set_ifreq_addr(fd, name, SIOCSIFNETMASK, mask) != 0 ||
	    set_ifreq_addr(fd, name, SIOCSIFBRDADDR, broadcast) != 0 ||
	    set_if_up(fd, name) != 0)
		return -1;

	if (lease->router) {
		memset(&rt, 0, sizeof(rt));
		sin = (struct sockaddr_in *)&rt.rt_dst;
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = INADDR_ANY;
		sin = (struct sockaddr_in *)&rt.rt_genmask;
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = INADDR_ANY;
		sin = (struct sockaddr_in *)&rt.rt_gateway;
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = lease->router;
		rt.rt_flags = RTF_UP | RTF_GATEWAY;
		strncpy(rt.rt_dev, name, IFNAMSIZ - 1);
		if (ioctl(fd, SIOCADDRT, &rt) != 0 && errno != EEXIST)
			return -1;
	}

	if (lease->dns) {
		FILE *fp = fopen("/etc/resolv.conf", "w");

		if (fp) {
			fprintf(fp, "nameserver %s\n", inet_ntoa(
					(struct in_addr){ .s_addr = lease->dns }));
			fclose(fp);
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	const char *ifname = argc > 1 ? argv[1] : "eth0";
	struct sockaddr_in local = {
		.sin_family = AF_INET,
		.sin_port = htons(DHCP_CLIENT_PORT),
		.sin_addr.s_addr = INADDR_ANY,
	};
	struct dhcp_packet pkt;
	struct dhcp_lease offer, ack;
	uint8_t mac[6];
	uint32_t xid;
	size_t len;
	int fd;
	int attempt;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("dhcp: socket");
		return 1;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 },
		       sizeof(int)) != 0 ||
	    bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
		perror("dhcp: bind");
		close(fd);
		return 1;
	}
	if (get_mac(fd, ifname, mac) != 0 ||
	    set_if_up(fd, ifname) != 0) {
		perror("dhcp: interface");
		close(fd);
		return 1;
	}

	xid = (uint32_t)time(NULL) ^ (uint32_t)getpid();
	for (attempt = 0; attempt < 3; attempt++) {
		len = dhcp_build(&pkt, xid, mac, DHCP_DISCOVER, 0, 0);
		if (dhcp_send(fd, &pkt, len) < 0 ||
		    dhcp_wait(fd, xid, DHCP_OFFER, &offer) != 0) {
			xid++;
			continue;
		}

		len = dhcp_build(&pkt, xid, mac, DHCP_REQUEST, offer.yiaddr,
				 offer.server_id);
		if (dhcp_send(fd, &pkt, len) < 0 ||
		    dhcp_wait(fd, xid, DHCP_ACK, &ack) != 0) {
			xid++;
			continue;
		}

		if (configure_interface(fd, ifname, &ack) != 0) {
			perror("dhcp: configure");
			close(fd);
			return 1;
		}

		printf("dhcp: %s %s", ifname,
		       inet_ntoa((struct in_addr){ .s_addr = ack.yiaddr }));
		if (ack.mask)
			printf("/%s",
			       inet_ntoa((struct in_addr){ .s_addr = ack.mask }));
		if (ack.router)
			printf(" gw %s",
			       inet_ntoa((struct in_addr){ .s_addr = ack.router }));
		if (ack.dns)
			printf(" dns %s",
			       inet_ntoa((struct in_addr){ .s_addr = ack.dns }));
		printf("\n");
		close(fd);
		return 0;
	}

	fprintf(stderr, "dhcp: no lease on %s\n", ifname);
	close(fd);
	return 1;
}
