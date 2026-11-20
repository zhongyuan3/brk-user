#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Parse hh[:mm] and return seconds, or -1 for an invalid offset.
 */
static int parse_offset(const char **pp)
{
	const char *p = *pp;
	int hh = 0;
	int mm = 0;

	if (!isdigit((unsigned char)*p))
		return -1;

	while (isdigit((unsigned char)*p)) {
		hh = hh * 10 + (*p - '0');
		if (hh > 23)
			return -1;
		p++;
	}

	if (*p == ':') {
		p++;
		if (!isdigit((unsigned char)p[0]) ||
		    !isdigit((unsigned char)p[1]))
			return -1;
		mm = (p[0] - '0') * 10 + (p[1] - '0');
		if (mm > 59)
			return -1;
		p += 2;
	}

	*pp = p;
	return hh * 3600 + mm * 60;
}

/*
 * Return the local timezone offset east of UTC in seconds.
 *
 * TZ supports UTC/GMT, ISO-style UTC+08:00, POSIX-style CST-8 and
 * numeric POSIX offsets such as EST5.  Named zoneinfo files are not
 * available in the rootfs, so unknown region names fall back to UTC.
 */
long brk_tz_offset(void)
{
	const char *tz = getenv("TZ");
	const char *p;
	int sign;
	int offset;

	if (!tz || !tz[0])
		return 0;

	if (!strcmp(tz, "UTC") || !strcmp(tz, "GMT") ||
	    !strcmp(tz, "Etc/UTC") || !strcmp(tz, "Etc/GMT"))
		return 0;

	/* An explicit leading sign is treated as an ISO-style offset. */
	p = tz;
	if (*p == '+' || *p == '-') {
		sign = *p == '+' ? 1 : -1;
		p++;
		offset = parse_offset(&p);
		return offset < 0 ? 0 : sign * offset;
	}

	/* UTC+08:00 and GMT-5 are commonly used as ISO-style offsets. */
	if (!strncmp(p, "UTC", 3) || !strncmp(p, "GMT", 3)) {
		p += 3;
		if (!*p)
			return 0;
		if (*p != '+' && *p != '-')
			return 0;
		sign = *p == '+' ? 1 : -1;
		p++;
		offset = parse_offset(&p);
		return offset < 0 ? 0 : sign * offset;
	}

	/* POSIX std offset: the numeric offset is positive west of UTC. */
	while (isalpha((unsigned char)*p))
		p++;
	if (p == tz)
		return 0;

	sign = 1;
	if (*p == '+') {
		p++;
	} else if (*p == '-') {
		sign = -1;
		p++;
	} else if (!isdigit((unsigned char)*p)) {
		return 0;
	}

	offset = parse_offset(&p);
	if (offset < 0)
		return 0;
	return -(sign * offset);
}
