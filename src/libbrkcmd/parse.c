#include <brk/cmd.h>

#include <limits.h>
#include <stdlib.h>

int brk_cmd_parse_long(const char *s, long *out, long min, long max)
{
	char *end;
	long n;

	if (!s || !s[0]) {
		brk_cmd_error("invalid number");
		return -1;
	}

	n = strtol(s, &end, 10);
	if (*end != '\0') {
		brk_cmd_error("invalid number '%s'", s);
		return -1;
	}
	if (n < min || n > max) {
		brk_cmd_error("number out of range: '%s'", s);
		return -1;
	}

	*out = n;
	return 0;
}

int brk_cmd_parse_ulong(const char *s, unsigned long *out, unsigned long min,
			unsigned long max)
{
	char *end;
	unsigned long n;

	if (!s || !s[0]) {
		brk_cmd_error("invalid number");
		return -1;
	}

	n = strtoul(s, &end, 10);
	if (*end != '\0') {
		brk_cmd_error("invalid number '%s'", s);
		return -1;
	}
	if (n < min || n > max) {
		brk_cmd_error("number out of range: '%s'", s);
		return -1;
	}

	*out = n;
	return 0;
}

int brk_cmd_parse_pid(const char *s, pid_t *out)
{
	long n;

	if (brk_cmd_parse_long(s, &n, 1, LONG_MAX) != 0)
		return -1;

	*out = (pid_t)n;
	return 0;
}
