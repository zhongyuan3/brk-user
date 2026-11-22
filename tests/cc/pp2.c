/*
 * Preprocessor completion tests: # stringification, ## pasting,
 * macro-driven #include, invocations spanning several lines and the
 * __FILE__/__LINE__/__STDC__ builtins.  Everything is checked in
 * memory and the exit code reports the verdict.
 */
#include <cc.h>

#define HDR "ppm.h"
#include HDR

#define STR(x) #x
#define XSTR(x) STR(x)
#define CAT(a, b) a##b
#define XCAT(a, b) CAT(a, b)
#define PASTE3(a, b, c) a##b##c

static int fails;

static void putn(long v)
{
	if (v < 0) {
		putchar('-');
		v = -v;
	}
	if (v >= 10)
		putn(v / 10);
	putchar((int)('0' + v % 10));
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		++a, ++b;
	return *a == *b;
}

static void check(int ok, const char *what)
{
	if (!ok) {
		puts("pp2 FAIL: ");
		puts(what);
		putchar('\n');
		fails++;
	}
}

#define FORTY_TWO 42
#define AAB_ 99
#define AA 7
#define B_ 8
#define ONE_M 1
#define TWO_M 2

#define ADD3(a, b, c) ((a) + (b) + (c))

int main(void)
{
	int line_here = __LINE__;
	int v;

	check(line_here == __LINE__ - 3, "__LINE__ adjacent");
	check(__STDC__ == 1, "__STDC__");

	check(streq(STR(abc), "abc"), "STR literal");
	check(streq(STR(FORTY_TWO), "FORTY_TWO"),
	      "STR does not expand its arg");
	check(streq(XSTR(FORTY_TWO), "42"),
	      "indirect STR expands first");
	check(streq(STR(  spaced  ), "spaced"), "STR trims spaces");

	v = CAT(1, 2);
	check(v == 12, "CAT numbers");
	v = XCAT(ONE_M, TWO_M);
	check(v == 12, "XCAT expands params before pasting");
	v = CAT(AA, B_);
	check(v == 99,
	      "paste operands stay raw; the result rescans");
	v = PASTE3(9, 8, 7);
	check(v == 987, "triple pasting");
	check(streq(XSTR(CAT(he, llo)), "hello"),
	      "paste makes an identifier");

	check(PPM_VALUE == 77, "macro-named include");

	v = ADD3(1,
		 2,
		 3);
	check(v == 6, "multi-line invocation");
	v = ADD3(ADD3(1, 2, 3),
		 4,
		 ADD3(5, 6, 7));
	check(v == 28, "nested multi-line invocation");

	check(line_here > 10, "__LINE__ sanity");
	check(streq(__FILE__, "/home/tests/cc/pp2.c"), "__FILE__");

	if (fails) {
		putn(fails);
		puts(" failure(s)\n");
		return 1;
	}
	puts("pp2 ok\n");
	return 0;
}
