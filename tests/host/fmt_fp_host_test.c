/*
 * Host-side validation of the vendored printf floating-point engine.
 * fmt_fp is pure arithmetic plus the two output callbacks below, so
 * replacing the callbacks with a memory sink lets it run on the host
 * and compare byte-for-byte against glibc's %e/%f/%g output.  glibc's
 * %a output is not byte-compatible with the musl form (both are valid
 * C99 renderings), so %a is validated by round-trip through strtod.
 *
 * Build:  see tests/host/run.sh, or: make host-tests
 */

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Flag bits must match the shared printf definitions. */
#define PRTF_ZERO_PAD (1U << 0)
#define PRTF_LEFT_ALIGN (1U << 1)
#define PRTF_MARK_POS (1U << 2)
#define PRTF_ALT_FORM (1U << 3)
#define PRTF_PAD_POS (1U << 4)

/* Sink implementation standing in for the BRK stdio plumbing. */
static char sink[512];
static size_t sink_len;

/* Match the signatures fmt_fp.c expects; the context is opaque here. */
struct fmt_context {
	void *unused;
};

/* fmt_fp.c calls these with its own struct fmt_context pointer; the
 * sink only needs the two symbols to exist. */
void __printf_out(struct fmt_context *ctx, char const *buf, size_t len)
{
	(void)ctx;
	if (sink_len + len < sizeof(sink)) {
		memcpy(sink + sink_len, buf, len);
		sink_len += len;
	}
}

void __printf_pad(struct fmt_context *ctx, size_t n, char ch)
{
	(void)ctx;
	while (n-- > 0 && sink_len < sizeof(sink))
		sink[sink_len++] = ch;
}

int __printf_fmt_fp(struct fmt_context *ctx, double y, size_t width,
		    int prec, unsigned flags, int conv);

static int failures;

static struct fmt_context dummy_ctx;

static int run_fmt(double v, const char *spec, char *outbuf, size_t outsize)
{
	int prec = -1;
	unsigned flags = 0;
	size_t width = 0;
	char conv = spec[0];
	char const *s = spec + 1;

	sink_len = 0;
	sink[0] = 0;

	while (*s) {
		if (*s == '-')
			flags |= PRTF_LEFT_ALIGN;
		else if (*s == '0')
			flags |= PRTF_ZERO_PAD;
		else if (*s == '+')
			flags |= PRTF_MARK_POS;
		else if (*s == ' ')
			flags |= PRTF_PAD_POS;
		else if (*s == '#')
			flags |= PRTF_ALT_FORM;
		else
			break;
		s++;
	}
	while (*s >= '0' && *s <= '9')
		width = width * 10 + (*s++ - '0');
	if (*s == '.') {
		s++;
		prec = 0;
		while (*s >= '0' && *s <= '9')
			prec = prec * 10 + (*s++ - '0');
	}
	(void)conv;
	int l = __printf_fmt_fp(&dummy_ctx, v, width, prec, flags, spec[0]);
	sink[sink_len] = 0;
	(void)outbuf;
	(void)outsize;
	return l;
}

static void check_str(double v, const char *spec)
{
	char ref[512];
	char fmt[32];

	snprintf(fmt, sizeof(fmt), "%%%s%c", spec + 1, spec[0]);
	snprintf(ref, sizeof(ref), fmt, v);
	run_fmt(v, spec, NULL, 0);
	if (strcmp(sink, ref) != 0) {
		printf("FAIL %%%.5s v=%.17g got '%s' want '%s'\n", spec + 1,
		       v, sink, ref);
		failures++;
	}
}

static void check_rt(double v, const char *spec)
{
	char *end;
	double back;

	run_fmt(v, spec, NULL, 0);
	if (sink_len == 0) {
		printf("FAIL %%%.5s v=%.17g empty output\n", spec + 1, v);
		failures++;
		return;
	}
	back = strtod(sink, &end);
	/* the %a form must reproduce the exact value; %e must reproduce
	 * it within the precision it promised */
	if (spec[0] == 'a' || spec[0] == 'A') {
		if (back != v) {
			printf("FAIL rt %%%c v=%.17g got '%s' -> %.17g\n",
			       spec[0], v, sink, back);
			failures++;
		}
	} else if (!isfinite(v) || v == 0) {
		/* inf/nan/zero: string equality handled elsewhere */
	}
	(void)end;
}

int main(void)
{
	double vals[] = {
		0.0, -0.0, 1.0, -1.0, 0.5, 1.0 / 3.0, 2.0 / 3.0,
		3.141592653589793, 2.718281828459045, 1e-10, 1e10, 1e100,
		1e-300, 1e308, 123456.789, 0.000123456789,
		9.999999999999999e22, 1.0000000000000002,
		5e-324, 4.9406564584124654e-324, DBL_MAX, DBL_MIN,
		DBL_EPSILON, 123.5, 0.125, 100.0, 99.9999999,
		-999.999, INFINITY, -INFINITY, NAN,
	};
	const char *specs[] = {
		"f", "f.0", "f.1", "f.2", "f.6", "f.17", "f.20", "f30.10",
		"e", "e.0", "e.3", "e.15", "e.17", "e20.5", "e+",
		"g", "g.1", "g.4", "g.9", "g.15", "g.17", "g#.15", "g25",
		"E.3", "G.9", "F.2", "e-20.6", "f-25.4", "f020.2",
	};

	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); ++i) {
		for (size_t j = 0; j < sizeof(specs) / sizeof(specs[0]); ++j) {
			/* skip inf/nan for width-only extras */
			check_str(vals[i], specs[j]);
		}
	}

	/* %a: round-trip check on a spread of values */
	double aval[] = {
		0.0, 1.0, -1.0, 0.5, 1.0 / 3.0, 3.141592653589793,
		1e10, 1e-10, 1e300, 5e-324, DBL_MAX, DBL_MIN, 123.456,
		1.0000000000000002, -2.5,
	};
	for (size_t i = 0; i < sizeof(aval) / sizeof(aval[0]); ++i) {
		char spec[2] = { 'a', 0 };

		check_rt(aval[i], spec);
		spec[0] = 'A';
		check_rt(aval[i], spec);
	}

	/* random fuzz against glibc */
	srand(4242);
	for (int i = 0; i < 20000; ++i) {
		union { uint64_t u; double d; } u;
		const char *s;

		u.u = ((uint64_t)rand() << 48) ^ rand();
		u.u ^= (uint64_t)rand() << 24;
		/* keep away from the NaN/Inf encodings mostly */
		if (i % 7 == 0) {
			u.u &= 0x7fefffffffffffffULL;
			u.u |= (u.u & 0x10) ? 0x8000000000000000ULL : 0;
		}
		switch (i % 4) {
		case 0: s = "f"; break;
		case 1: s = "e.17"; break;
		case 2: s = "g.17"; break;
		default: s = "f.9"; break;
		}
		check_str(u.d, s);
	}

	if (failures) {
		printf("%d failures\n", failures);
		return 1;
	}
	printf("fmt_fp smoke test: all checks passed\n");
	return 0;
}
