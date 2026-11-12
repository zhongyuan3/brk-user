#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int ifreq_get(int fd, const char *name, unsigned long cmd,
		     struct ifreq *ifr)
{
	memset(ifr, 0, sizeof(*ifr));
	strncpy(ifr->ifr_name, name, IFNAMSIZ - 1);
	return ioctl(fd, cmd, ifr);
}

static int ifreq_set_addr(int fd, const char *name, unsigned long cmd,
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

static int set_up(int fd, const char *name, int up)
{
	struct ifreq ifr;

	if (ifreq_get(fd, name, SIOCGIFFLAGS, &ifr) != 0)
		return -1;
	if (up)
		ifr.ifr_flags = (short)(ifr.ifr_flags | IFF_UP);
	else
		ifr.ifr_flags = (short)(ifr.ifr_flags & ~IFF_UP);
	return ioctl(fd, SIOCSIFFLAGS, &ifr);
}

static void show_interface(int fd, const char *name)
{
	struct ifreq ifr;
	struct sockaddr_in *sin;
	const unsigned char *mac;
	uint32_t addr, mask, broadcast;
	int flags, mtu;
	int have_addr = 0;

	if (ifreq_get(fd, name, SIOCGIFFLAGS, &ifr) != 0)
		return;
	flags = ifr.ifr_flags;

	printf("%s: flags=0x%x", name, flags);
	if (flags & IFF_UP)
		printf(" UP");
	if (flags & IFF_BROADCAST)
		printf(" BROADCAST");
	if (flags & IFF_LOOPBACK)
		printf(" LOOPBACK");

	if (ifreq_get(fd, name, SIOCGIFMTU, &ifr) == 0) {
		mtu = ifr.ifr_mtu;
		printf(" mtu %d\n", mtu);
	} else {
		printf("\n");
	}

	if (ifreq_get(fd, name, SIOCGIFADDR, &ifr) == 0) {
		sin = (struct sockaddr_in *)&ifr.ifr_addr;
		addr = sin->sin_addr.s_addr;
		have_addr = 1;
	} else {
		addr = 0;
	}
	if (ifreq_get(fd, name, SIOCGIFNETMASK, &ifr) == 0) {
		sin = (struct sockaddr_in *)&ifr.ifr_netmask;
		mask = sin->sin_addr.s_addr;
	} else {
		mask = 0;
	}
	if (ifreq_get(fd, name, SIOCGIFBRDADDR, &ifr) == 0) {
		sin = (struct sockaddr_in *)&ifr.ifr_broadaddr;
		broadcast = sin->sin_addr.s_addr;
	} else {
		broadcast = 0;
	}

	if (have_addr || mask) {
		char addrbuf[16], maskbuf[16], bcastbuf[16];

		strncpy(addrbuf,
			inet_ntoa((struct in_addr){ .s_addr = addr }),
			sizeof(addrbuf) - 1);
		addrbuf[sizeof(addrbuf) - 1] = '\0';
		strncpy(maskbuf,
			inet_ntoa((struct in_addr){ .s_addr = mask }),
			sizeof(maskbuf) - 1);
		maskbuf[sizeof(maskbuf) - 1] = '\0';
		strncpy(bcastbuf,
			inet_ntoa((struct in_addr){ .s_addr = broadcast }),
			sizeof(bcastbuf) - 1);
		bcastbuf[sizeof(bcastbuf) - 1] = '\0';
		printf("        inet %s netmask %s broadcast %s\n", addrbuf,
		       maskbuf, bcastbuf);
	}

	if (ifreq_get(fd, name, SIOCGIFHWADDR, &ifr) == 0) {
		mac = (const unsigned char *)ifr.ifr_hwaddr.sa_data;
		printf("        ether %02x:%02x:%02x:%02x:%02x:%02x\n", mac[0],
		       mac[1], mac[2], mac[3], mac[4], mac[5]);
	}
}

int main(int argc, char *argv[])
{
	const char *name;
	struct in_addr addr, mask;
	uint32_t broadcast;
	int fd;
	int err;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("ifconfig: socket");
		return 1;
	}

	if (argc == 1) {
		show_interface(fd, "lo");
		show_interface(fd, "eth0");
		close(fd);
		return 0;
	}

	name = argv[1];
	if (argc == 2) {
		show_interface(fd, name);
		close(fd);
		return 0;
	}

	if (!strcmp(argv[2], "up") || !strcmp(argv[2], "down")) {
		err = set_up(fd, name, !strcmp(argv[2], "up"));
	} else {
		if (!inet_aton(argv[2], &addr)) {
			fprintf(stderr, "ifconfig: invalid address: %s\n",
				argv[2]);
			close(fd);
			return 1;
		}
		err = ifreq_set_addr(fd, name, SIOCSIFADDR, addr.s_addr);
		if (err == 0) {
			if (argc >= 5 && !strcmp(argv[3], "netmask") &&
			    inet_aton(argv[4], &mask)) {
				err = ifreq_set_addr(fd, name, SIOCSIFNETMASK,
						     mask.s_addr);
				broadcast = addr.s_addr | ~mask.s_addr;
				if (err == 0)
					err = ifreq_set_addr(
						fd, name, SIOCSIFBRDADDR,
						broadcast);
			}
		}
		if (err == 0)
			err = set_up(fd, name, 1);
	}

	if (err != 0) {
		fprintf(stderr, "ifconfig: %s: %s\n", name, strerror(errno));
		close(fd);
		return 1;
	}

	show_interface(fd, name);
	close(fd);
	return 0;
}
