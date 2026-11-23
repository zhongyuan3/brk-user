/*
 * Floating literal lexing tests: decimal forms, exponents and the
 * f/F suffix.  This test also exercises the front end and code
 * generator once they support TK_FLOAT.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;

static int close_to(double a, double b)
{
	double d = a - b;

	if (d < 0)
		d = -d;
	return d < 1e-9;
}

static void check(int ok, const char *what)
{
	if (!ok) {
		puts("pp_float FAIL: ");
		puts(what);
		putchar('\n');
		fails++;
	}
}

int main(void)
{
	double a = 1.0;
	double b = .5;
	double c = 1.;
	double d = 1e3;
	double e = 1.5e-2;
	double f = 1.5e+2;
	double g = 100.0e-2;
	double h = 1e-3;
	float i = 2.5f;
	float j = 0.125F;

	check(a == 1.0, "1.0");
	check(b == 0.5, ".5");
	check(c == 1.0, "1.");
	check(d == 1000.0, "1e3");
	check(close_to(e, 0.015), "1.5e-2");
	check(f == 150.0, "1.5e+2");
	check(g == 1.0, "100.0e-2");
	check(h == 0.001, "1e-3");
	check(i == 2.5, "f suffix");
	check(j == 0.125, "F suffix");
	check(sizeof(float) == 4, "float is 32-bit");

	if (fails) {
		puts("pp_float failed\n");
		return 1;
	}
	puts("pp_float ok\n");
	return 0;
}
