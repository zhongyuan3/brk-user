/*
 * Preprocessor tests: #pragma once, #line and the predefined
 * __LINE__/__FILE__/__DATE__/__TIME__/__STDC_VERSION__ macros.
 * Everything is checked in memory and the exit code reports the
 * verdict.
 */
#pragma once
#include "pp_line.c"
#include "./pp_line.c"

#include <cc.h>

#if !defined(__STDC__) || !defined(__DATE__) || !defined(__TIME__)
#error predefined macros must be visible to defined()
#endif
#if __STDC_VERSION__ < 201112L
#error __STDC_VERSION__ is too old
#endif

static int fails;

static int slen(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	return n;
}

static int streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		++a, ++b;
	return *a == *b;
}

static int ends_with(const char *s, const char *suffix)
{
	int n = slen(s);
	int m = slen(suffix);

	if (m > n)
		return 0;
	return streq(s + n - m, suffix);
}

static void check(int ok, const char *what)
{
	if (!ok) {
		puts("pp_line FAIL: ");
		puts(what);
		putchar('\n');
		fails++;
	}
}

#line 500 "virtual-pp.c"
static const char *line_file = __FILE__;
static int line_first = __LINE__;

#line 700
static int line_second = __LINE__;
static const char *line_file2 = __FILE__;

#define NEXT_LINE 901
#line NEXT_LINE
static int line_third = __LINE__;

int main(void)
{
#line 1234 "pp_line.c"
	int here = __LINE__;
	const char *date = __DATE__;
	const char *tmstr = __TIME__;

	check(line_first == 501, "#line sets __LINE__");
	check(streq(line_file, "virtual-pp.c"), "#line sets __FILE__");
	check(line_second == 700, "#line without a file name");
	check(streq(line_file2, "virtual-pp.c"), "#line keeps __FILE__");
	check(line_third == 901, "#line expands its argument");
	check(here == 1234, "#line applies to the next line");
	check(ends_with(__FILE__, "pp_line.c"), "#line resets __FILE__");
	check(__STDC__ == 1, "__STDC__");
	check(__STDC_VERSION__ == 201112L, "__STDC_VERSION__");
	check(slen(date) == 11 && date[3] == ' ' && date[6] == ' ',
	      "__DATE__ format");
	check(slen(tmstr) == 8 && tmstr[2] == ':' && tmstr[5] == ':',
	      "__TIME__ format");

	if (fails) {
		puts("pp_line failed\n");
		return 1;
	}
	puts("pp_line ok\n");
	return 0;
}
