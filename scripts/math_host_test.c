/*
 * Host-side smoke test for the vendored musl math routines.
 *
 * The BRK math code is pure bit manipulation and IEEE-754 arithmetic with
 * no syscall or ABI dependencies, so it can be built and validated on the
 * host: the results here transfer to the riscv64 build unchanged.
 * The vendored .c files are compiled as separate translation units with
 * -Dname=brk_name renaming (so their prototypes and cross-calls stay
 * consistent) and linked with this driver, which compares every function
 * against glibc libm and requires agreement within 2 ULP.
 *
 * Build:  see scripts/math-host-test.sh
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* BRK implementations, renamed at compile time. */
double brk_exp(double);
double brk_log(double);
double brk_log2(double);
double brk_log10(double);
double brk_pow(double, double);
double brk_sin(double);
double brk_cos(double);
double brk_tan(double);
double brk_asin(double);
double brk_acos(double);
double brk_atan(double);
double brk_atan2(double, double);
double brk_fmod(double, double);
double brk_floor(double);
double brk_ceil(double);
double brk_fabs(double);
double brk_frexp(double, int *);
double brk_ldexp(double, int);
double brk_scalbn(double, int);
double brk_modf(double, double *);
double brk_sqrt(double);
double brk_copysign(double, double);
int brk___fpclassify(double);
int brk___signbit(double);

static int failures;

static double ulp_diff(double got, double want)
{
	if (isnan(got) && isnan(want))
		return 0;
	if (isinf(got) || isinf(want))
		return (got == want) ? 0 : HUGE_VAL;
	if (got == want)
		return 0;
	/* Both finite and different: measure the gap in ULPs. */
	union { double d; uint64_t i; } g = { got }, w = { want };
	if ((g.i >> 63) != (w.i >> 63))
		return HUGE_VAL; /* sign mismatch */
	int64_t d = (int64_t)(g.i & INT64_MAX) - (int64_t)(w.i & INT64_MAX);
	double ad = (double)(d < 0 ? -d : d);
	/* Adjacent doubles near 0/subnormals: treat any tiny gap as 1 ULP. */
	return ad < 1 ? 1 : ad;
}

#define CHECK_1ARG(name, xs)                                              \
	do {                                                              \
		for (size_t t_ = 0; t_ < sizeof(xs) / sizeof((xs)[0]); ++t_) { \
			double x_ = (xs)[t_];                          \
			double got_ = brk_##name(x_);                   \
			double want_ = name(x_);                        \
			double e_ = ulp_diff(got_, want_);              \
			if (e_ > 2) {                                   \
				printf("FAIL %-6s(%.17g) got %.17g want %.17g (%g ulp)\n", \
				       #name, x_, got_, want_, e_);        \
				failures++;                            \
			}                                               \
		}                                                   \
	} while (0)

#define CHECK_2ARG(name, xs, ys)                                           \
	do {                                                              \
		for (size_t t_ = 0; t_ < sizeof(xs) / sizeof((xs)[0]); ++t_) { \
			double x_ = (xs)[t_];                          \
			double y_ = (ys)[t_];                          \
			double got_ = brk_##name(x_, y_);              \
			double want_ = name(x_, y_);                    \
			double e_ = ulp_diff(got_, want_);              \
			if (e_ > 2) {                                   \
				printf("FAIL %-6s(%.17g, %.17g) got %.17g want %.17g (%g ulp)\n", \
				       #name, x_, y_, got_, want_, e_);   \
				failures++;                            \
			}                                               \
		}                                                   \
	} while (0)

int main(void)
{
	double spread1[256];
	size_t n1 = 0;

	spread1[n1++] = 0.0;
	spread1[n1++] = -0.0;
	spread1[n1++] = 1e-300;
	spread1[n1++] = -1e-300;
	spread1[n1++] = 0.5;
	spread1[n1++] = -0.5;
	spread1[n1++] = 0.001;
	spread1[n1++] = 0.25;
	spread1[n1++] = 0.7;
	spread1[n1++] = 1.0;
	spread1[n1++] = 1.5;
	spread1[n1++] = 2.0;
	spread1[n1++] = 2.718281828459045;
	spread1[n1++] = 3.141592653589793;
	spread1[n1++] = 10.0;
	spread1[n1++] = 123.456;
	spread1[n1++] = -0.001;
	spread1[n1++] = -1.0;
	spread1[n1++] = -1.5;
	spread1[n1++] = -2.0;
	spread1[n1++] = -100.0;
	spread1[n1++] = 1e10;
	spread1[n1++] = 1e100;
	spread1[n1++] = 1e300;
	/* trig gets arguments across many reduction classes */
	for (double a = -25.0; a <= 25.0; a += 1.0 / 3.0)
		spread1[n1++] = a;
	spread1[n1++] = 1e8;
	spread1[n1++] = 123456789.123;
	spread1[n1++] = 0x1p1023;
	spread1[n1++] = 5e-324; /* smallest subnormal */
	spread1[n1++] = 1.0 + 0x1p-52; /* one ulp above 1 */
	spread1[n1++] = 1.0 - 0x1p-53;

	CHECK_1ARG(sqrt, spread1);
	if (!isnan(brk_sqrt(-1.0))) { printf("FAIL sqrt(-1) not NaN\n"); failures++; }
	if (brk_fabs(-3.5) != 3.5) { printf("FAIL fabs\n"); failures++; }

	double pos1[64];
	size_t np = 0;
	pos1[np++] = 0.001;
	pos1[np++] = 0.25;
	pos1[np++] = 0.7;
	pos1[np++] = 1.0;
	pos1[np++] = 1.5;
	pos1[np++] = 2.0;
	pos1[np++] = 2.718281828459045;
	pos1[np++] = 3.141592653589793;
	pos1[np++] = 10.0;
	pos1[np++] = 123.456;
	pos1[np++] = 1e10;
	pos1[np++] = 1e100;
	pos1[np++] = 1e300;
	pos1[np++] = 0.9999999999;
	pos1[np++] = 1.0000000001;
	pos1[np++] = 2.0 / 3.0;
	pos1[np++] = 1e-8;
	pos1[np++] = 5e-324;
	pos1[np++] = 1.0 + 0x1p-52;

	CHECK_1ARG(log, pos1);
	CHECK_1ARG(log2, pos1);
	CHECK_1ARG(log10, pos1);
	CHECK_1ARG(exp, spread1);
	CHECK_1ARG(sin, spread1);
	CHECK_1ARG(cos, spread1);
	CHECK_1ARG(tan, spread1);
	CHECK_1ARG(floor, spread1);
	CHECK_1ARG(ceil, spread1);
	CHECK_1ARG(atan, spread1);

	double unit[] = { -1.0, -0.99999, -0.9, -0.5, -0.1, -1e-8, 0.0, 1e-8, 0.1, 0.5, 0.9, 0.99999, 1.0, 0.7071067811865476 };
	CHECK_1ARG(asin, unit);
	CHECK_1ARG(acos, unit);

	CHECK_2ARG(pow, ((double[]){ 0.5, 1.5, 2.0, 3.0, 10.0, 1e-3, 1e10, -2.0, 1.0, 0.0 }),
			    ((double[]){ 2.0, 3.0, 0.5, -0.5, 100.0, 0.5, -3.0, 3.0, 1e10, 0.0 }));
	CHECK_2ARG(atan2, ((double[]){ 1.0, -1.0, 0.5, 3.0, -1e-8, 1e8, 0.0, -0.0, 1.0, 1.0 }),
			     ((double[]){ 1.0, 1.0, -2.0, -0.5, 1.0, -1e8, -1.0, -1.0, 0.0, 1e-8 }));
	CHECK_2ARG(fmod, ((double[]){ 5.3, -5.3, 5.3, 1e300, -7.0, 7.0, 0x1p52, 3.0 }),
			     ((double[]){ 2.0, 2.0, -2.0, 1e-300, 2.5, -7.0, 3.0, 1.0 }));

	for (size_t i = 0; i < n1; ++i) {
		double x = spread1[i];
		if (x == 0 || !isfinite(x))
			continue;
		int e1, e2;
		double m1 = brk_frexp(x, &e1);
		double m2 = frexp(x, &e2);
		if (m1 != m2 || e1 != e2) {
			printf("FAIL frexp(%.17g)\n", x);
			failures++;
		}
		double back = brk_ldexp(m1, e1);
		if (back != x) {
			printf("FAIL ldexp(frexp(%.17g)) -> %.17g\n", x, back);
			failures++;
		}
		double i1, i2;
		double f1 = brk_modf(x, &i1);
		double f2 = modf(x, &i2);
		if (f1 != f2 || i1 != i2) {
			printf("FAIL modf(%.17g)\n", x);
			failures++;
		}
	}

	if (brk___fpclassify(0.0) != FP_ZERO || brk___fpclassify(5e-324) != FP_SUBNORMAL ||
	    brk___fpclassify(1.0) != FP_NORMAL || brk___fpclassify(INFINITY) != FP_INFINITE ||
	    brk___fpclassify(0.0 / 0.0) != FP_NAN) {
		printf("FAIL fpclassify\n");
		failures++;
	}
	if (!brk___signbit(-0.0) || brk___signbit(0.0)) {
		printf("FAIL signbit\n");
		failures++;
	}
	if (brk_copysign(3.0, -1.0) != -3.0) {
		printf("FAIL copysign\n");
		failures++;
	}

	if (failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("math smoke test: all checks passed (%zu values)\n", n1);
	return 0;
}
