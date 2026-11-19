/*
 * Host-side validation of the vendored strtod engine (floatscan-derived).
 * The scanner is pure arithmetic and string walking with no OS
 * dependencies, so host results transfer to the riscv64 build.
 *
 * Build:  cc -O2 -o /tmp/strtod_host_test strtod_host_test.c
 */

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The vendored scanner is renamed at compile time by the build below;
 * declare the interface here. */
double brk_strtod(const char *s, char **p);

static int failures;

static void check(const char *s, double want, int check_errno)
{
	double got;
	char *p1 = (char *)0xdead;
	char *p2 = (char *)0xbeef;
	int e1, e2;

	errno = 0;
	got = brk_strtod(s, &p1);
	e1 = errno;
	errno = 0;
	double glibc = strtod(s, &p2);
	e2 = errno;
	(void)glibc;

	if (isnan(want)) {
		if (!isnan(got)) {
			printf("FAIL %-30s expected NaN got %.17g\n", s, got);
			failures++;
		}
		return;
	}
	if (got != want) {
		printf("FAIL %-30s got %.17g want %.17g\n", s, got, want);
		failures++;
	}
	/* endptr: both consumed the same prefix */
	if (p1 - s != p2 - s) {
		printf("FAIL %-30s endptr offset %ld want %ld\n", s,
		       (long)(p1 - s), (long)(p2 - s));
		failures++;
	}
	if (check_errno && (e1 != 0) != (e2 != 0)) {
		printf("FAIL %-30s errno %d want %d\n", s, e1, e2);
		failures++;
	}
}

static void check_both(const char *s)
{
	/* agreement test: compare against glibc directly */
	char *p1 = NULL, *p2 = NULL;
	double got, want;
	int e1, e2;

	errno = 0;
	got = brk_strtod(s, &p1);
	e1 = errno;
	errno = 0;
	want = strtod(s, &p2);
	e2 = errno;

	if (isnan(got) != isnan(want) || (!isnan(got) && got != want)) {
		printf("FAIL %-34s got %.17g want %.17g\n", s, got, want);
		failures++;
	}
	if (p1 - s != p2 - s) {
		printf("FAIL %-34s endptr %ld want %ld\n", s,
		       (long)(p1 - s), (long)(p2 - s));
		failures++;
	}
	if ((e1 != 0) != (e2 != 0)) {
		printf("FAIL %-34s errno %d want %d\n", s, e1, e2);
		failures++;
	}
}

int main(void)
{
	check("0", 0.0, 0);
	check("1", 1.0, 0);
	check("-1", -1.0, 0);
	check("3.141592653589793", 3.141592653589793, 0);
	check("0.1", 0.1, 0);
	check("1e10", 1e10, 0);
	check("1e-10", 1e-10, 0);
	check("1e308", 1e308, 0);
	check("1e309", HUGE_VAL, 1); /* overflow */
	check("-1e309", -HUGE_VAL, 1);
	check("1e-320", 1e-320, 1); /* subnormal */
	check("4.9406564584124654e-324", 4.9406564584124654e-324, 1);
	check("2.2250738585072011e-308", 2.2250738585072011e-308, 0);
	check("0x1p4", 16.0, 0);
	check("0x1.8p1", 3.0, 0);
	check("0x10", 16.0, 0);
	check("inf", HUGE_VAL, 0);
	check("-inf", -HUGE_VAL, 0);
	check("infinity", HUGE_VAL, 0);
	check("nan", NAN, 0);
	check("-nan(123)", -NAN, 0);
	check("1.7976931348623157e308", DBL_MAX, 0);
	check("9007199254740993", 9007199254740992.0, 0);
	/* exact halfway rounding cases */
	check("9007199254740992.5", 9007199254740992.0, 0);
	check("9007199254740993.5", 9007199254740994.0, 0);
	check("2.2250738585072012e-308", 2.2250738585072014e-308, 0);

	/* endptr behavior */
	{
		char *p;
		(void)brk_strtod("  +12.5x", &p);
		if (strcmp(p, "x") != 0) {
			printf("FAIL endptr: trailing at '%s'\n", p);
			failures++;
		}
	}
	{
		char *p;
		double v = brk_strtod("junk", &p);
		if (v != 0.0 || p == NULL) {
			printf("FAIL no-conversion: v=%g p=%s\n", v, p ? p : "");
			failures++;
		}
	}

	/* a sweep of random digit strings, compared with glibc */
	{
		char buf[64];
		srand(12345);
		for (int i = 0; i < 20000; ++i) {
			int nd = 1 + rand() % 17;
			int ne = -40 + rand() % 80;
			char *w = buf;
			for (int d = 0; d < nd; ++d)
				*w++ = '0' + rand() % 10;
			*w++ = '.';
			for (int d = 0; d < 6; ++d)
				*w++ = '0' + rand() % 10;
			sprintf(w, "e%d", ne);
			check_both(buf);
		}
	}

	/* long digit strings (exercise the big-decimal path) */
	{
		char buf[600];
		for (size_t i = 0; i < sizeof(buf) - 1; ++i)
			buf[i] = '0' + (int)(i * 7 % 10);
		buf[sizeof(buf) - 1] = 0;
		check_both(buf);
		check_both("0.00000000000000000000000000000000001e-30");
	}

	if (failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("strtod smoke test: all checks passed\n");
	return 0;
}
