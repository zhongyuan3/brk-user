#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

uint32_t htonl(uint32_t hostlong)
{
	return __builtin_bswap32(hostlong);
}

uint16_t htons(uint16_t hostshort)
{
	return __builtin_bswap16(hostshort);
}

uint32_t ntohl(uint32_t netlong)
{
	return __builtin_bswap32(netlong);
}

uint16_t ntohs(uint16_t netshort)
{
	return __builtin_bswap16(netshort);
}

static int parse_octet(const char **pp, unsigned int *out)
{
	const char *p = *pp;
	unsigned int v = 0;
	int digits = 0;

	while (*p >= '0' && *p <= '9') {
		v = v * 10 + (unsigned int)(*p - '0');
		if (v > 255)
			return 0;
		p++;
		digits++;
	}
	if (!digits)
		return 0;
	*pp = p;
	*out = v;
	return 1;
}

int inet_aton(const char *cp, struct in_addr *inp)
{
	const char *p = cp;
	unsigned int parts[4];
	unsigned char bytes[4];

	if (!cp || !inp)
		return 0;

	for (int i = 0; i < 4; i++) {
		if (!parse_octet(&p, &parts[i]))
			return 0;
		if (i < 3) {
			if (*p != '.')
				return 0;
			p++;
		}
	}
	if (*p != '\0')
		return 0;

	/* The kernel stores IPv4 addresses in wire byte order. */
	for (int i = 0; i < 4; i++)
		bytes[i] = (unsigned char)parts[i];
	memcpy(&inp->s_addr, bytes, sizeof(bytes));
	return 1;
}

in_addr_t inet_addr(const char *cp)
{
	struct in_addr in;

	if (!inet_aton(cp, &in))
		return INADDR_NONE;
	return in.s_addr;
}

char *inet_ntoa(struct in_addr in)
{
	static char buf[16];
	const unsigned char *p = (const unsigned char *)&in.s_addr;

	snprintf(buf, sizeof(buf), "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
	return buf;
}
