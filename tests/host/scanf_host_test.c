/*
 * Host-side validation of the scanf engine against glibc.
 *
 * The engine is pure string walking over the vendored strtod, so the
 * sscanf results transfer to the riscv64 build.  Every case checks
 * the return value and every assigned output against glibc.
 *
 * Build:  see tests/host/run.sh, or: make host-tests
 */

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The BRK engine is renamed to brk_* at compile time by
 * tests/host/run.sh; declare the interfaces here.
 */
int brk_sscanf(const char *str, const char *format, ...);
int brk_vsscanf(const char *str, const char *format, va_list ap);

static int failures;
static int checks;

static void begin(void)
{
	checks++;
	errno = 0;
}

static void fail(const char *what, const char *input, const char *fmt)
{
	printf("FAIL %-28s input=\"%s\" fmt=\"%s\"\n", what, input, fmt);
	failures++;
}

static void check_ret(const char *input, const char *fmt, int got,
		      int want)
{
	if (got != want)
		fail("return value", input, fmt);
}

#define SCAN1(input, fmt, out) do {					\
	begin();							\
	{								\
		typeof(out) g = (typeof(out))0, b = (typeof(out))0;	\
		int rb = brk_sscanf(input, fmt, &b);			\
		int rg = sscanf(input, fmt, &g);				\
		check_ret(input, fmt, rb, rg);				\
		if (rb != rg || (rg > 0 &&				\
				 memcmp(&b, &g, sizeof(b)) != 0))	\
			fail("value", input, fmt);			\
		(void)(out);						\
	}								\
} while (0)

static void test_ints(void)
{
	int d;
	long l;
	unsigned u;
	unsigned long long ull;
	short h;
	signed char hh;
	size_t z;

	SCAN1("42", "%d", d);
	SCAN1("  -42xyz", "%d", d);
	SCAN1("+7", "%d", d);
	SCAN1("0", "%d", d);
	SCAN1("", "%d", d);
	SCAN1("abc", "%d", d);
	SCAN1("2147483647", "%d", d);
	SCAN1("2147483648", "%d", d);		/* overflow clamps */
	SCAN1("-2147483649", "%d", d);
	SCAN1("999999999999999999999", "%d", d);

	SCAN1("123456789012", "%ld", l);
	{
		long long q = 0;
		long long gq = 0;
		int rb, rg;

		begin();
		rb = brk_sscanf("-12345678901234567890", "%lld", &q);
		rg = sscanf("-12345678901234567890", "%lld", &gq);
		check_ret("-12345678901234567890", "%lld", rb, rg);
		if (rb != rg || q != gq)
			fail("lld clamp", "-12345678901234567890", "%lld");
	}
	SCAN1("18446744073709551615", "%llu", ull);
	SCAN1("18446744073709551616", "%llu", ull);/* clamps */

	SCAN1("4294967295", "%u", u);
	SCAN1("-1", "%u", u);			/* strtoul negation */
	SCAN1("377", "%o", u);
	SCAN1("378", "%o", u);			/* stops at the 8 */
	SCAN1("ff", "%x", u);
	SCAN1("0xffz", "%x", u);
	SCAN1("0x", "%x", u);			/* the lone 0 */
	SCAN1("0Xg", "%x", u);
	SCAN1("deadBEEF", "%x", u);

	SCAN1("0x1f", "%i", d);			/* prefix detect */
	SCAN1("017", "%i", d);
	SCAN1("08", "%i", d);			/* octal stops at 8 */
	SCAN1("12", "%i", d);

	SCAN1("-300", "%hhd", hh);		/* narrows after clamp */
	SCAN1("70000", "%hd", h);
	SCAN1("10", "%zu", z);
}

static void test_floats(void)
{
	double v;
	float f;

	SCAN1("3.25", "%lf", v);
	SCAN1("-0.5", "%lf", v);
	SCAN1("1e10", "%lf", v);
	SCAN1("1e", "%lf", v);			/* bare exponent */
	SCAN1("1e+", "%lf", v);
	SCAN1("1.5e-3q", "%lf", v);
	SCAN1(".5", "%lf", v);
	SCAN1("5.", "%lf", v);
	SCAN1(".", "%lf", v);			/* not a number */
	SCAN1("..", "%lf", v);
	SCAN1("1.2.3", "%lf", v);		/* second dot ends it */
	SCAN1("0x1p4", "%lf", v);		/* hex float */
	SCAN1("0x1p", "%lf", v);
	SCAN1("0x8p-2", "%lf", v);
	SCAN1("0x", "%lf", v);			/* plain 0 */
	SCAN1("0xz", "%lf", v);
	SCAN1("inf", "%lf", v);
	SCAN1("-infinity", "%lf", v);
	SCAN1("infi", "%lf", v);			/* no conversion */
	SCAN1("nan", "%lf", v);
	SCAN1("NAN(0)", "%lf", v);		/* nan(...) tail stays */
	SCAN1("1e999", "%lf", v);		/* ERANGE clamps */
	SCAN1("1e-999", "%lf", v);
	SCAN1("1234567890123456789012345678901234567890.5", "%lf", v);

	SCAN1("2.5", "%f", f);
	SCAN1("-1e3", "%f", f);
}

static void test_strings(void)
{
	char a[32], b[32];
	char ga[32], gb[32];
	int rb, rg;

	begin();
	rb = brk_sscanf("hello world", "%s %s", a, b);
	rg = sscanf("hello world", "%s %s", ga, gb);
	check_ret("hello world", "%s %s", rb, rg);
	if (rb != 2 || strcmp(a, "hello") || strcmp(b, "world"))
		fail("two words", "hello world", "%s %s");

	begin();
	rb = brk_sscanf("  padded", "%s", a);
	rg = sscanf("  padded", "%s", ga);
	check_ret("  padded", "%s", rb, rg);
	if (rb != 1 || strcmp(a, "padded"))
		fail("leading ws", "  padded", "%s");

	begin();
	rb = brk_sscanf("abc", "%3s", a);
	rg = sscanf("abc", "%3s", ga);
	check_ret("abc", "%3s", rb, rg);
	if (strcmp(a, ga))
		fail("value", "abc", "%3s");

	begin();
	rb = brk_sscanf("abcdef", "%3s", a);
	rg = sscanf("abcdef", "%3s", ga);
	check_ret("abcdef", "%3s", rb, rg);
	if (strcmp(a, "abc") || strcmp(ga, "abc"))
		fail("width cut", "abcdef", "%3s");

	begin();
	rb = brk_sscanf("", "%s", a);
	rg = sscanf("", "%s", ga);
	check_ret("", "%s", rb, rg);

	begin();
	rb = brk_sscanf("   ", "%s", a);
	rg = sscanf("   ", "%s", ga);
	check_ret("   ", "%s", rb, rg);
}

static void test_scansets(void)
{
	char a[32];

	begin();
	{
		int rb = brk_sscanf("hello123", "%[a-z]", a);
		int rg = sscanf("hello123", "%[a-z]", a);

		check_ret("hello123", "%[a-z]", rb, rg);
		(void)rg;
		if (rb != rg || strcmp(a, "hello"))
			fail("range set", "hello123", "%[a-z]");
	}
	begin();
	{
		int rb = brk_sscanf("  hello", "%[^ ]", a);
		int rg = sscanf("  hello", "%[^ ]", a);

		check_ret("  hello", "%[^ ]", rb, rg);
	}
	begin();
	{
		int rb = brk_sscanf("a]b", "%[]a]", a);
		int rg = sscanf("a]b", "%[]a]", a);

		check_ret("a]b", "%[]a]", rb, rg);
	}
	begin();
	{
		int rb = brk_sscanf("a-b", "%[a-]", a);
		int rg = sscanf("a-b", "%[a-]", a);

		check_ret("a-b", "%[a-]", rb, rg);
	}
	begin();
	{
		int rb = brk_sscanf("xyz", "%[^x]", a);
		int rg = sscanf("xyz", "%[^x]", a);

		check_ret("xyz", "%[^x]", rb, rg);
	}
}

/* Variadic forwarder so va_start is used legally. */
static int vsscanf_runner(const char *str, const char *fmt, ...)
{
	va_list ap;
	int rc;

	va_start(ap, fmt);
	rc = brk_vsscanf(str, fmt, ap);
	va_end(ap);
	return rc;
}

static void test_mixed(void)
{
	char s[32], ga[32];
	int d;
	double v;

	begin();
	{
		char s4[32], g4[32];
		int rb = brk_sscanf("foo 12 -1.5e2 rest", "%s %d %lf %s",
				    s, &d, &v, s4);
		int rg = sscanf("foo 12 -1.5e2 rest", "%s %d %lf %s",
				ga, &d, &v, g4);

		(void)ga;
		check_ret("foo 12 -1.5e2 rest", "%s %d %lf %s", rb, rg);
		if (rb != 4 || strcmp(s, "foo") || d != 12 || v != -150.0 ||
		    strcmp(s4, "rest"))
			fail("mixed values", "foo 12 -1.5e2 rest",
			     "%s %d %lf %s");
	}

	/* literal text and %% must match exactly */
	begin();
	{
		int rb = brk_sscanf("n=5", "n=%d", &d);
		int rg = sscanf("n=5", "n=%d", &d);

		check_ret("n=5", "n=%d", rb, rg);
	}
	begin();
	{
		int rb = brk_sscanf("m=5", "n=%d", &d);
		int rg = sscanf("m=5", "n=%d", &d);

		check_ret("m=5", "n=%d", rb, rg);
	}
	begin();
	{
		int rb = brk_sscanf("25%", "%d%%", &d);
		int rg = sscanf("25%", "%d%%", &d);

		check_ret("25%", "%d%%", rb, rg);
	}

	/* %n reports the consumed count without counting as a match */
	begin();
	{
		int n = -1;
		int rb = brk_sscanf("42xy", "%d%n", &d, &n);
		int rg;

		(void)rb;
		rg = sscanf("42xy", "%d%n", &d, &n);
		check_ret("42xy", "%d%n", rb, rg);
		if (n != 2)
			fail("%n count", "42xy", "%d%n");
	}

	/* suppression */
	begin();
	{
		int rb = brk_sscanf("1 2 3", "%*d %d", &d);
		int rg = sscanf("1 2 3", "%*d %d", &d);

		check_ret("1 2 3", "%*d %d", rb, rg);
	}

	/* %c reads exactly width chars, no skip */
	begin();
	{
		char c4[8] = { 0 };
		char g4[8] = { 0 };
		int rb = brk_sscanf(" ab", "%4c", c4);
		int rg = sscanf(" ab", "%4c", g4);

		check_ret(" ab", "%4c", rb, rg);
		if (memcmp(c4, g4, sizeof(c4)) != 0)
			fail("%c content", " ab", "%4c");
	}
	begin();
	{
		char c1 = 0;
		int rb = brk_sscanf("z", "%c", &c1);
		int rg = sscanf("z", "%c", &c1);

		(void)rg;
		check_ret("z", "%c", rb, 1);
		if (c1 != 'z')
			fail("%c char", "z", "%c");
	}

	/* EOF before any conversion completes */
	begin();
	{
		int rb = brk_sscanf("", "%d", &d);
		int rg = sscanf("", "%d", &d);

		check_ret("", "%d", rb, rg);
	}
	begin();
	{
		int rb = brk_sscanf("", "%n", &d);
		int rg = sscanf("", "%n", &d);

		check_ret("", "%n", rb, rg);
	}
	begin();
	{
		int rb = brk_sscanf("", " %d", &d);
		int rg = sscanf("", " %d", &d);

		check_ret("", " %d", rb, rg);
	}
	begin();
	{
		int rb = brk_sscanf("junk", "%d", &d);
		int rg = sscanf("junk", "%d", &d);

		check_ret("junk", "%d", rb, rg);
	}

	/* vsscanf agrees with sscanf */
	begin();
	{
		int dd = 0;
		double vv = 0;
		int rb = vsscanf_runner("7 0.5", "%d %lf", &dd, &vv);

		if (rb != 2 || dd != 7 || vv != 0.5)
			fail("vsscanf", "7 0.5", "%d %lf");
	}
}

int main(void)
{
	test_ints();
	test_floats();
	test_strings();
	test_scansets();
	test_mixed();

	if (failures) {
		printf("scanf host test: %d failure(s)\n", failures);
		return 1;
	}
	printf("scanf host test: all checks passed (%d cases)\n", checks);
	return 0;
}
