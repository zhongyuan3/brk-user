/*
 * RISC-V LP64D calling-convention coverage: FP registers, mixed
 * integer/FP arguments, stack arguments and aggregate classification.
 * Expected output:
 *   3 1 2 3
 *   12
 *   66
 *   4 6
 *   45
 *   66
 *   10
 *   55
 *   66
 *   58
 *   88
 *   42
 */
#include <cc.h>

struct S {
	double x;
	double y;
};

struct U {
	float a;
	int b;
};

struct T {
	int a;
	int b;
	int c;
};

struct V {
	long a;
	long b;
	long c;
};

struct P {
	long a;
	long b;
};

void putnum(int n)
{
	if (n < 0) {
		putchar('-');
		n = -n;
	}
	if (n >= 10)
		putnum(n / 10);
	putchar('0' + n % 10);
}

struct S gs(struct S s)
{
	s.x += 1;
	s.y += 2;
	return s;
}

struct U gu(struct U u)
{
	u.a += 0.5f;
	u.b += 4;
	return u;
}

struct T gt(struct T t)
{
	t.a += 1;
	t.b += 2;
	t.c += 3;
	return t;
}

struct V gv(struct V v)
{
	v.a += 1;
	v.b += 2;
	v.c += 3;
	return v;
}

int sum9(int a, int b, int c, int d, int e, int f, int g, int h, int i)
{
	return a + b + c + d + e + f + g + h + i;
}

double df(double a, int b, double c, int d, double e, int f, double g,
	  int h, double i, int j, double k)
{
	return a + b + c + d + e + f + g + h + i + j + k;
}

double dsum10(double a, double b, double c, double d, double e, double f,
	      double g, double h, double i, double j)
{
	return a + b + c + d + e + f + g + h + i + j;
}

double vf9(int a, int b, int c, int d, int e, int f, int g, double x0,
	   double x1, double x2, double x3, double x4, double x5, double x6,
	   double x7, double x8, ...)
{
	va_list ap;
	double r;

	va_start(ap, x8);
	r = x8 + va_arg(ap, double) + va_arg(ap, double);
	va_end(ap);
	return r + a + b + c + d + e + f + g + x0 + x1 + x2 + x3 + x4 +
	       x5 + x6 + x7;
}

long split8(int a, int b, int c, int d, int e, int f, int g, struct P p)
{
	return a + b + c + d + e + f + g + p.a + p.b;
}

double mix8(double a, double b, double c, double d, double e, double f,
	    double g, struct S s, int q)
{
	return a + b + c + d + e + f + g + s.x + s.y + q;
}

double mixu(double a, double b, double c, double d, double e, double f,
	    double g, double h, struct U u, int q)
{
	return a + b + c + d + e + f + g + h + u.a + u.b + q;
}

long vsum(int n, ...)
{
	va_list ap;
	long s = 0;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		s += va_arg(ap, long);
	va_end(ap);
	return s;
}

int main(void)
{
	struct S s = { 2.5, 4.5 };
	struct U u = { 1.5f, 2 };
	struct T t = { 1, 2, 3 };
	struct V v = { 10, 20, 30 };

	s = gs(s);
	putnum((int)s.x);
	putchar(' ');
	putnum((int)u.a);
	putchar(' ');
	putnum(u.b);
	putchar(' ');
	putnum(t.c);
	putchar('\n');

	t = gt(t);
	putnum(t.a + t.b + t.c);
	putchar('\n');

	v = gv(v);
	putnum((int)(v.a + v.b + v.c));
	putchar('\n');

	s = gs(s);
	u = gu(u);
	putnum((int)s.x);
	putchar(' ');
	putnum(u.b);
	putchar('\n');

	putnum(sum9(1, 2, 3, 4, 5, 6, 7, 8, 9));
	putchar('\n');

	putnum((int)df(1.0, 2, 3.0, 4, 5.0, 6, 7.0, 8, 9.0, 10, 11.0));
	putchar('\n');

	putnum((int)vsum(4, 1L, 2L, 3L, 4L));
	putchar('\n');

	/* Ninth and tenth named doubles fall back to the integer registers. */
	putnum((int)dsum10(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0,
			   10.0));
	putchar('\n');

	/*
	 * Seven named integer registers are full; the ninth named double
	 * takes a7 and the variadic doubles start on the stack.
	 */
	putnum((int)vf9(0, 0, 0, 0, 0, 0, 0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0,
			7.0, 8.0, 9.0, 10.0, 11.0));
	putchar('\n');

	/* A two-word aggregate splits between a7 and the stack. */
	{
		struct P p = { 10, 20 };

		putnum(split8(1, 2, 3, 4, 5, 6, 7, p));
		putchar('\n');
	}

	/* Two-FP struct with one FP register left falls back to integer regs. */
	{
		struct S s2 = { 10.0, 20.0 };

		putnum((int)mix8(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, s2, 30));
		putchar('\n');
	}

	/* Mixed FP/int struct with no FP register left also uses integer regs. */
	{
		struct U u2 = { 1.5f, 2 };

		putnum((int)mixu(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0,
				 u2, 3));
		putchar('\n');
	}
	return 0;
}
