#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DNS_PORT 53
#define DNS_BUF 1500

static unsigned short next_id;

static unsigned short get16(const unsigned char *p)
{
	return (unsigned short)(((unsigned short)p[0] << 8) | p[1]);
}

static void put16(unsigned char *p, unsigned short v)
{
	p[0] = (unsigned char)(v >> 8);
	p[1] = (unsigned char)v;
}

static int dns_nameserver(struct in_addr *addr)
{
	FILE *fp = fopen("/etc/resolv.conf", "r");
	char line[256];
	int found = 0;

	if (fp) {
		while (fgets(line, sizeof(line), fp)) {
			char *p = line;

			while (*p == ' ' || *p == '\t')
				p++;
			if (strncmp(p, "nameserver", 10))
				continue;
			p += 10;
			while (*p == ' ' || *p == '\t')
				p++;
			char *end = p;

			while (*end && *end != '\n' && *end != '\r' &&
			       *end != ' ' && *end != '\t')
				end++;
			*end = '\0';
			if (inet_aton(p, addr)) {
				found = 1;
				break;
			}
		}
		fclose(fp);
	}

	if (!found)
		return inet_aton("10.0.2.3", addr) ? 0 : -1;
	return 0;
}

static int dns_build(const char *name, unsigned char *buf, size_t size,
		     unsigned short id)
{
	size_t off = 12;
	const char *p = name;

	if (size < 12 || strlen(name) > 253)
		return -1;

	memset(buf, 0, 12);
	put16(buf, id);
	put16(buf + 2, 0x0100); /* recursion desired */
	put16(buf + 4, 1);

	while (*p) {
		const char *label = p;
		size_t len;

		while (*p && *p != '.')
			p++;
		len = (size_t)(p - label);
		if (!len || len > 63 || off + 1 + len >= size)
			return -1;
		buf[off++] = (unsigned char)len;
		memcpy(buf + off, label, len);
		off += len;
		if (*p == '.')
			p++;
	}
	if (off + 5 > size)
		return -1;
	buf[off++] = 0;
	put16(buf + off, 1); /* A */
	off += 2;
	put16(buf + off, 1); /* IN */
	off += 2;
	return (int)off;
}

static int skip_name(const unsigned char *buf, size_t len, size_t *off)
{
	while (*off < len) {
		unsigned char c = buf[*off];

		if (c == 0) {
			(*off)++;
			return 0;
		}
		if ((c & 0xc0) == 0xc0) {
			if (*off + 1 >= len)
				return -1;
			*off += 2;
			return 0;
		}
		if (c > 63)
			return -1;
		*off += (size_t)c + 1;
	}
	return -1;
}

static int dns_parse(const unsigned char *buf, size_t len, unsigned short id,
		     struct in_addr *addr)
{
	size_t off = 12;
	unsigned int qd, an;

	if (len < 12 || get16(buf) != id)
		return -1;
	if ((get16(buf + 2) & 0x000f) != 0)
		return -1;

	qd = get16(buf + 4);
	an = get16(buf + 6);
	for (unsigned int i = 0; i < qd; i++) {
		if (skip_name(buf, len, &off) || off + 4 > len)
			return -1;
		off += 4;
	}

	for (unsigned int i = 0; i < an; i++) {
		if (skip_name(buf, len, &off) || off + 10 > len)
			return -1;
		unsigned short type = get16(buf + off);
		unsigned short class = get16(buf + off + 2);
		unsigned short rdlen = get16(buf + off + 8);

		off += 10;
		if (off + rdlen > len)
			return -1;
		if (type == 1 && class == 1 && rdlen == 4) {
			memcpy(&addr->s_addr, buf + off, 4);
			return 0;
		}
		off += rdlen;
	}
	return -1;
}

static int dns_lookup(const char *name, struct in_addr *addr)
{
	struct in_addr ns;
	unsigned char query[512];
	unsigned char reply[DNS_BUF];
	int qlen, fd;
	unsigned short id = ++next_id;

	if (dns_nameserver(&ns) != 0)
		return -1;
	qlen = dns_build(name, query, sizeof(query), id);
	if (qlen < 0)
		return -1;

	struct sockaddr_in sa = {
		.sin_family = AF_INET,
		.sin_port = htons(DNS_PORT),
		.sin_addr = ns,
	};

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;
	if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		close(fd);
		return -1;
	}

	if (send(fd, query, (size_t)qlen, 0) < 0 && errno != EAGAIN) {
		close(fd);
		return -1;
	}

	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int ret = poll(&pfd, 1, 5000);

	if (ret <= 0 || !(pfd.revents & POLLIN)) {
		close(fd);
		return -1;
	}

	ssize_t n = recv(fd, reply, sizeof(reply), 0);

	close(fd);
	if (n < 0)
		return -1;
	return dns_parse(reply, (size_t)n, id, addr);
}

struct hostent *gethostbyname(const char *name)
{
	static char namebuf[256];
	static unsigned char addr[4];
	static char *aliases[1];
	static char *addrlist[2];
	static struct hostent host;
	struct in_addr in;

	if (!name || !name[0] || strlen(name) >= sizeof(namebuf))
		return NULL;

	if (!strcmp(name, "localhost"))
		in.s_addr = INADDR_LOOPBACK;
	else if (!inet_aton(name, &in) && dns_lookup(name, &in) != 0)
		return NULL;

	memcpy(addr, &in.s_addr, 4);
	strcpy(namebuf, name);

	aliases[0] = NULL;
	addrlist[0] = (char *)addr;
	addrlist[1] = NULL;
	host.h_name = namebuf;
	host.h_aliases = aliases;
	host.h_addrtype = AF_INET;
	host.h_length = 4;
	host.h_addr_list = addrlist;
	return &host;
}
