#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PING_COUNT 4
#define PING_PAYLOAD "brk-ping"

struct icmp_echo {
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
	uint16_t id;
	uint16_t sequence;
	uint8_t payload[8];
} __attribute__((packed));

static uint16_t icmp_checksum(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint32_t sum = 0;

	while (len > 1) {
		sum += ((uint32_t)p[0] << 8) | p[1];
		p += 2;
		len -= 2;
	}
	if (len)
		sum += (uint32_t)p[0] << 8;
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);
	return htons((uint16_t)~sum);
}

static int resolve(const char *name, struct in_addr *addr)
{
	struct hostent *he;

	if (inet_aton(name, addr))
		return 0;
	he = gethostbyname(name);
	if (!he || he->h_addrtype != AF_INET || he->h_length != 4)
		return -1;
	memcpy(&addr->s_addr, he->h_addr, 4);
	return 0;
}

int main(int argc, char *argv[])
{
	struct sockaddr_in sa = { .sin_family = AF_INET };
	struct icmp_echo packet;
	struct icmp_echo reply;
	struct in_addr addr;
	unsigned int count = PING_COUNT;
	int fd;
	int seq;

	if (argc < 2) {
		fprintf(stderr, "usage: ping HOST [COUNT]\n");
		return 1;
	}
	if (argc > 2) {
		char *end;
		unsigned long value = strtoul(argv[2], &end, 10);

		if (*end || value == 0 || value > 1000) {
			fprintf(stderr, "ping: invalid count: %s\n", argv[2]);
			return 1;
		}
		count = (unsigned int)value;
	}

	if (resolve(argv[1], &addr) != 0) {
		fprintf(stderr, "ping: cannot resolve %s\n", argv[1]);
		return 1;
	}
	sa.sin_addr = addr;

	fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (fd < 0) {
		perror("ping: socket");
		return 1;
	}

	memset(&packet, 0, sizeof(packet));
	packet.type = 8; /* ICMP echo request */
	packet.id = htons((uint16_t)getpid());

	for (seq = 1; seq <= (int)count; seq++) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		struct sockaddr_in from;
		socklen_t fromlen = sizeof(from);
		struct icmp_echo *icmp;
		ssize_t n;
		int ret;

		packet.sequence = htons((uint16_t)seq);
		packet.checksum = 0;
		memcpy(packet.payload, PING_PAYLOAD, sizeof(packet.payload));
		packet.checksum = icmp_checksum(&packet, sizeof(packet));

		if (sendto(fd, &packet, sizeof(packet), 0,
			   (struct sockaddr *)&sa, sizeof(sa)) < 0 &&
		    errno != EAGAIN) {
			perror("ping: sendto");
			close(fd);
			return 1;
		}

		for (;;) {
			ret = poll(&pfd, 1, 1000);
			if (ret <= 0) {
				printf("ping: seq=%d timeout\n", seq);
				break;
			}
			fromlen = sizeof(from);
			n = recvfrom(fd, &reply, sizeof(reply), 0,
				     (struct sockaddr *)&from, &fromlen);
			if (n < (ssize_t)sizeof(*icmp))
				continue;
			icmp = &reply;
			if (icmp->type != 0 ||
			    ntohs(icmp->id) != (uint16_t)getpid() ||
			    ntohs(icmp->sequence) != (uint16_t)seq)
				continue;
			printf("%d bytes from %s: icmp_seq=%d\n", (int)n,
			       inet_ntoa(from.sin_addr),
			       (int)ntohs(icmp->sequence));
			break;
		}
	}

	close(fd);
	return 0;
}
