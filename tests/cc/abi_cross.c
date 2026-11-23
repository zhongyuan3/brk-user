/*
 * Cross-compiler LP64D ABI coverage.  This translation unit is compiled by
 * the on-target cc and linked with tests/cc/abi_gcc.c, which is compiled by
 * the cross GCC.  Calls go in both directions, including FP fallback,
 * aggregate splitting, hidden returns, small-register returns and varargs.
 * Expected output:
 *   3 5
 *   36
 *   14
 *   600
 *   650
 *   45
 *   12
 *   100
 *   94
 *   39
 *   58
 *   45
 *   291
 *   855
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

struct DS {
	double a;
	double b;
};

struct III {
	int a;
	int b;
	int c;
};

struct FX {
	float f;
	int i;
};

struct BIG {
	long a;
	long b;
	long c;
};

struct LL {
	long a;
	long b;
};

struct BF {
	unsigned int a:3;
	unsigned int b:5;
	unsigned int c:24;
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

struct DS cc_ret_ds(double a, double b)
{
	struct DS s;

	s.a = a;
	s.b = b;
	return s;
}

struct III cc_ret_iii(int a, int b, int c)
{
	struct III v;

	v.a = a;
	v.b = b;
	v.c = c;
	return v;
}

struct FX cc_ret_fx(float f, int i)
{
	struct FX v;

	v.f = f;
	v.i = i;
	return v;
}

struct BIG cc_ret_big(long a, long b, long c)
{
	struct BIG v;

	v.a = a;
	v.b = b;
	v.c = c;
	return v;
}

struct BF cc_ret_bf(unsigned a, unsigned b, unsigned c)
{
	struct BF v;

	v.a = a;
	v.b = b;
	v.c = c;
	return v;
}

unsigned cc_take_bf(struct BF v)
{
	return (v.a << 8) + (v.b << 4) + v.c;
}

long cc_take(struct DS s, struct III t, struct FX u, struct BIG v)
{
	return (long)s.a + (long)s.b + t.a + t.b + t.c + (long)u.f + u.i +
	       v.a + v.b + v.c;
}

long cc_split7(int a, int b, int c, int d, int e, int f, int g, struct LL p)
{
	return a + b + c + d + e + f + g + p.a + p.b;
}

double cc_sum9d(double a, double b, double c, double d, double e, double f,
		double g, double h, double i)
{
	return a + b + c + d + e + f + g + h + i;
}

double cc_vsum(int n, ...)
{
	va_list ap;
	double sum = 0;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		sum += va_arg(ap, double);
	va_end(ap);
	return sum;
}

/* GCC-built functions. */
struct DS gcc_ret_ds(double a, double b);
struct III gcc_ret_iii(int a, int b, int c);
struct FX gcc_ret_fx(float f, int i);
struct BIG gcc_ret_big(long a, long b, long c);
struct LL gcc_ret_ll(long a, long b);
long gcc_take(struct DS s, struct III t, struct FX u, struct BIG v);
long gcc_split7(int a, int b, int c, int d, int e, int f, int g,
		struct LL p);
struct BF gcc_ret_bf(unsigned a, unsigned b, unsigned c);
unsigned gcc_take_bf(struct BF v);
double gcc_sum9d(double a, double b, double c, double d, double e, double f,
		 double g, double h, double i);
double gcc_vsum(int n, ...);
long gcc_vsumi(int n, ...);
long gcc_call_cc(void);
long gcc_call_split(void);
long gcc_call_cc9(void);
long gcc_call_cc_bf(void);

int main(void)
{
	struct DS s;
	struct III t;
	struct FX u;
	struct BIG v;
	struct LL p;

	s = gcc_ret_ds(1.5, 2.5);
	t = gcc_ret_iii(11, 12, 13);
	u = gcc_ret_fx(2.5f, 9);
	v = gcc_ret_big(100, 200, 300);
	p = gcc_ret_ll(5, 6);

	putnum((int)(s.a * 2));
	putchar(' ');
	putnum((int)(s.b * 2));
	putchar('\n');

	putnum(t.a + t.b + t.c);
	putchar('\n');

	putnum((int)(u.f * 2) + u.i);
	putchar('\n');

	putnum((int)(v.a + v.b + v.c));
	putchar('\n');

	putnum((int)gcc_take(s, t, u, v));
	putchar('\n');

	putnum((int)gcc_sum9d(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0));
	putchar('\n');

	putnum((int)gcc_vsum(4, 1.5, 2.5, 3.5, 4.5));
	putchar('\n');

	putnum((int)gcc_vsumi(4, 10L, 20L, 30L, 40L));
	putchar('\n');

	putnum((int)gcc_call_cc());
	putchar('\n');

	putnum((int)gcc_split7(1, 2, 3, 4, 5, 6, 7, p));
	putchar('\n');

	putnum((int)gcc_call_split());
	putchar('\n');

	putnum((int)gcc_call_cc9());
	putchar('\n');

	putnum((int)gcc_take_bf(gcc_ret_bf(1, 2, 3)));
	putchar('\n');

	putnum((int)gcc_call_cc_bf());
	putchar('\n');
	return 0;
}
