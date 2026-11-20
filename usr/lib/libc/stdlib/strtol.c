#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static int digit_value(char c, int base)
{
	int val;
	if ('0' <= c && c <= '9')
		val = c - '0';
	else if ('a' <= c && c <= 'z')
		val = c - 'a' + 10;
	else if ('A' <= c && c <= 'Z')
		val = c - 'A' + 10;
	else
		return -1;
	return (val < base) ? val : -1;
}

static unsigned long long strtoull_core(const char *nptr, char **endptr,
					int base, int *neg)
{
	const char *s = nptr;
	unsigned long long acc = 0;
	int overflow = 0;
	int digit;
	int detected_base = base;

	while (isspace((unsigned char)*s))
		s++;

	*neg = 0;
	if (*s == '+')
		s++;
	else if (*s == '-') {
		*neg = 1;
		s++;
	}

	if (detected_base == 0) {
		if (*s == '0') {
			if (s[1] == 'x' || s[1] == 'X') {
				detected_base = 16;
				s += 2;
			} else {
				detected_base = 8;
			}
		} else {
			detected_base = 10;
		}
	} else if (detected_base == 16) {
		if (*s == '0' && (s[1] == 'x' || s[1] == 'X'))
			s += 2;
	}

	while ((digit = digit_value(*s, detected_base)) >= 0) {
		if (acc > (ULLONG_MAX - digit) / detected_base) {
			overflow = 1;
			acc = ULLONG_MAX;
		}
		if (!overflow)
			acc = acc * detected_base + digit;
		s++;
	}

	if (endptr)
		*endptr = (char *)(s == nptr ? nptr : s);

	if (overflow) {
		errno = ERANGE;
		return ULLONG_MAX;
	}
	return acc;
}

long strtol(const char *nptr, char **endptr, int base)
{
	int neg;
	unsigned long long val = strtoull_core(nptr, endptr, base, &neg);

	if (val > (unsigned long)LONG_MAX) {
		if (neg && val == (unsigned long)LONG_MAX + 1UL) {
			errno = 0;
			return LONG_MIN;
		}
		errno = ERANGE;
		return neg ? LONG_MIN : LONG_MAX;
	}

	if (neg)
		return -(long)val;
	else
		return (long)val;
}

long long strtoll(const char *nptr, char **endptr, int base)
{
	int neg;
	unsigned long long val = strtoull_core(nptr, endptr, base, &neg);

	if (val > (unsigned long long)LLONG_MAX) {
		if (neg && val == (unsigned long long)LLONG_MAX + 1ULL) {
			errno = 0;
			return LLONG_MIN;
		}
		errno = ERANGE;
		return neg ? LLONG_MIN : LLONG_MAX;
	}

	if (neg)
		return -(long long)val;
	else
		return (long long)val;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
	int neg;
	unsigned long long val = strtoull_core(nptr, endptr, base, &neg);

	if (neg)
		return (unsigned long)(0ULL - val);

	if (val > ULONG_MAX) {
		errno = ERANGE;
		return ULONG_MAX;
	}
	return (unsigned long)val;
}

unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
	int neg;
	unsigned long long val = strtoull_core(nptr, endptr, base, &neg);

	if (neg)
		return 0ULL - val;
	return val;
}
