/*
 * GCC-built side of the cross-compiler LP64D ABI test.  The on-target cc
 * compiles tests/cc/abi_cross.c and links it with this object.
 */
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

struct DS gcc_ret_ds(double a, double b)
{
	struct DS s;

	s.a = a;
	s.b = b;
	return s;
}

struct III gcc_ret_iii(int a, int b, int c)
{
	struct III v;

	v.a = a;
	v.b = b;
	v.c = c;
	return v;
}

struct FX gcc_ret_fx(float f, int i)
{
	struct FX v;

	v.f = f;
	v.i = i;
	return v;
}

struct BIG gcc_ret_big(long a, long b, long c)
{
	struct BIG v;

	v.a = a;
	v.b = b;
	v.c = c;
	return v;
}

struct LL gcc_ret_ll(long a, long b)
{
	struct LL v;

	v.a = a;
	v.b = b;
	return v;
}

struct BF gcc_ret_bf(unsigned a, unsigned b, unsigned c)
{
	struct BF v;

	v.a = a;
	v.b = b;
	v.c = c;
	return v;
}

unsigned gcc_take_bf(struct BF v)
{
	return (v.a << 8) + (v.b << 4) + v.c;
}

long gcc_take(struct DS s, struct III t, struct FX u, struct BIG v)
{
	return (long)s.a + (long)s.b + t.a + t.b + t.c + (long)u.f + u.i +
	       v.a + v.b + v.c;
}

long gcc_split7(int a, int b, int c, int d, int e, int f, int g, struct LL p)
{
	return a + b + c + d + e + f + g + p.a + p.b;
}

double gcc_sum9d(double a, double b, double c, double d, double e, double f,
		  double g, double h, double i)
{
	return a + b + c + d + e + f + g + h + i;
}

double gcc_vsum(int n, ...)
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

long gcc_vsumi(int n, ...)
{
	va_list ap;
	long sum = 0;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		sum += va_arg(ap, long);
	va_end(ap);
	return sum;
}

/* Functions defined by the cc side. */
struct DS cc_ret_ds(double a, double b);
struct III cc_ret_iii(int a, int b, int c);
struct FX cc_ret_fx(float f, int i);
struct BIG cc_ret_big(long a, long b, long c);
long cc_take(struct DS s, struct III t, struct FX u, struct BIG v);
long cc_split7(int a, int b, int c, int d, int e, int f, int g, struct LL p);
struct BF cc_ret_bf(unsigned a, unsigned b, unsigned c);
unsigned cc_take_bf(struct BF v);
double cc_sum9d(double a, double b, double c, double d, double e, double f,
		double g, double h, double i);
double cc_vsum(int n, ...);

long gcc_call_cc(void)
{
	struct DS s = cc_ret_ds(6.5, 7.5);
	struct III t = cc_ret_iii(1, 2, 3);
	struct FX u = cc_ret_fx(1.5f, 4);
	struct BIG v = cc_ret_big(5, 6, 7);
	long r;

	r = cc_take(s, t, u, v);
	r += (long)cc_vsum(3, 2.5, 3.5, 4.0);
	r += (long)s.a + (long)s.b + t.a + t.b + t.c + (long)u.f + u.i +
	     v.a + v.b + v.c;
	return r;
}

long gcc_call_split(void)
{
	struct LL p;

	p.a = 10;
	p.b = 20;
	return cc_split7(1, 2, 3, 4, 5, 6, 7, p);
}

long gcc_call_cc9(void)
{
	return (long)cc_sum9d(1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);
}

long gcc_call_cc_bf(void)
{
	struct BF v = cc_ret_bf(3, 5, 7);

	return cc_take_bf(v);
}
