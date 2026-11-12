#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static void set_inet_addr(struct sockaddr *sa, uint32_t addr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;

	memset(sin, 0, sizeof(*sin));
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = addr;
}

int main(int argc, char *argv[])
{
	struct in_addr gw_addr, dst_addr, mask_addr;
	struct rtentry rt;
	const char *ifname = "eth0";
	bool add;
	int fd;
	int err;

	if (argc < 5 || (strcmp(argv[2], "default") &&
			 strcmp(argv[2], "0.0.0.0"))) {
		fprintf(stderr,
			"usage: route add|del default gw ADDRESS [dev IFNAME]\n");
		return 1;
	}

	add = !strcmp(argv[1], "add");
	if (!add && strcmp(argv[1], "del")) {
		fprintf(stderr, "route: expected add or del\n");
		return 1;
	}
	if (strcmp(argv[3], "gw") || !inet_aton(argv[4], &gw_addr)) {
		fprintf(stderr, "route: expected gw ADDRESS\n");
		return 1;
	}
	if (argc >= 7 && !strcmp(argv[5], "dev"))
		ifname = argv[6];

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("route: socket");
		return 1;
	}

	inet_aton("0.0.0.0", &dst_addr);
	inet_aton("0.0.0.0", &mask_addr);
	memset(&rt, 0, sizeof(rt));
	set_inet_addr(&rt.rt_dst, dst_addr.s_addr);
	set_inet_addr(&rt.rt_genmask, mask_addr.s_addr);
	set_inet_addr(&rt.rt_gateway, gw_addr.s_addr);
	rt.rt_flags = RTF_UP | RTF_GATEWAY;
	strncpy(rt.rt_dev, ifname, IFNAMSIZ - 1);

	err = ioctl(fd, add ? SIOCADDRT : SIOCDELRT, &rt);
	if (err != 0)
		fprintf(stderr, "route: %s: %s\n", add ? "add" : "del",
			strerror(errno));
	close(fd);
	return err ? 1 : 0;
}
