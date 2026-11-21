#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "scan_impl.h"

/*
 * The scanf conversion engine shared by the fscanf and sscanf
 * families.  Every conversion looks at most two speculative
 * characters past what it keeps, so the retraction ring in
 * scan_impl.h never overflows:
 *
 *   - integers accumulate incrementally with strtol/strtoul
 *     semantics: optional sign, %i detects the 0x/0 prefixes
 *     (retracting the 'x' when no hex digit follows), digits are
 *     consumed whole even on overflow, and the value saturates the
 *     way strtol/strtoul do before narrowing to the target type.
 *   - floats collect the longest prefix strtod could accept into a
 *     buffer (a stack window, grown on the heap for long digit
 *     strings so correct rounding keeps working), convert with the
 *     vendored correctly rounded strtod, then retract the unused
 *     tail.  errno == ERANGE still assigns the clamped value, like
 *     every strtod-based scanf.
 *   - %c, %s and %[...] consume exactly what they assign and never
 *     retract; %n reports the consumed count; %% matches literally.
 *
 * Only double and NUL-terminated byte strings are provided: the
 * long-modifier wide-character forms (ls, lc, l[) stop the scan.
 */

enum {
	LEN_NONE,
	LEN_HH,
	LEN_H,
	LEN_L,
	LEN_LL,
	LEN_Z,
	LEN_J,
	LEN_T,
};

static int digval(int c, int base)
{
	int d;

	if (c >= '0' && c <= '9')
		d = c - '0';
	else if (c >= 'a' && c <= 'z')
		d = c - 'a' + 10;
	else if (c >= 'A' && c <= 'Z')
		d = c - 'A' + 10;
	else
		return -1;
	return d < base ? d : -1;
}

static void skip_ws(struct scan_src *s)
{
	int c;

	do {
		c = scan_getc(s);
	} while (c != EOF && isspace(c));
	scan_ungetc(s, c);
}

/* Append one character to the collection buffer, growing if needed. */
static int buf_push(char **bufp, size_t *capp, size_t *lenp, char sbuf[],
		   int ch)
{
	if (*lenp + 1 >= *capp) {
		char *grown = malloc(*capp * 2);

		if (!grown)
			return 0;
		memcpy(grown, *bufp, *lenp);
		if (*bufp != sbuf)
			free(*bufp);
		*bufp = grown;
		*capp *= 2;
	}
	(*bufp)[(*lenp)++] = (char)ch;
	return 1;
}

/*
 * Integer conversion with strtol/strtoul semantics, matching both
 * glibc and musl: the magnitude accumulates in 64 bits, true
 * overflow clamps (LLONG_MAX/LLONG_MIN or ULLONG_MAX), and the store
 * narrows modularly to the target type.  A missing digit after a
 * sign and a 0x prefix without a hex digit fail the directive.
 * Returns 1 on success, 0 on a matching failure, -1 when the source
 * was exhausted before the first character.
 */
static int scan_int(struct scan_src *s, int width, int base, int unsignedp,
		    int *negp, unsigned long long *accp, int *ovp)
{
	long left = width ? width : INT_MAX;
	unsigned long long acc = 0;
	int neg = 0;
	int any = 0;
	int ov = 0;
	int c, d;

	c = scan_getc(s);
	if (c == EOF)
		return -1;

	if (c == '+' || c == '-') {
		neg = c == '-';
		if (--left == 0)
			return 0;	/* a lone sign retracts itself */
		c = scan_getc(s);
		if (c == EOF)
			return 0;
	}

	if ((base == 0 || base == 16) && c == '0' && left > 1) {
		int c2 = scan_getc(s);

		if (c2 == 'x' || c2 == 'X') {
			int c3 = left > 2 ? scan_getc(s) : EOF;

			if (c3 != EOF && digval(c3, 16) >= 0) {
				base = 16;
				left -= 2;	/* the 0x prefix */
				c = c3;
				goto digits;
			}
			/* "0x" without a hex digit fails the directive. */
			scan_ungetc(s, c3);
			scan_ungetc(s, c2);
			return 0;
		}
		scan_ungetc(s, c2);
		if (base == 0)
			base = 8;
	} else if (base == 0) {
		base = c == '0' ? 8 : 10;
	}

digits:
	for (;;) {
		d = digval(c, base);
		if (d < 0) {
			scan_ungetc(s, c);
			break;
		}
		unsigned long long limit = unsignedp ?
			ULLONG_MAX : (unsigned long long)INT64_MAX;

		any = 1;
		if (!ov) {
			if (acc > (limit - (unsigned)d) / (unsigned)base)
				ov = 1;	/* clamp like strtol */
			else
				acc = acc * (unsigned)base + (unsigned)d;
		}
		if (--left == 0)
			break;
		c = scan_getc(s);
		if (c == EOF)
			break;
	}

	if (!any)
		return 0;
	*negp = neg;
	*accp = acc;
	*ovp = ov;
	return 1;
}

/*
 * Float conversion.  Collects the candidate, converts with strtod
 * and retracts the unused tail.  Returns 1, 0 or -1 like scan_int.
 */
static int scan_float(struct scan_src *s, int width, double *out)
{
	char sbuf[64];
	char *buf = sbuf;
	size_t cap = sizeof(sbuf);
	size_t len = 0;
	long left = width ? width : INT_MAX;
	int dot = 0;
	int hex = 0;
	int c;
	char *end;
	double v;

	c = scan_getc(s);
	if (c == EOF)
		return -1;

	if (c == '+' || c == '-') {
		if (!buf_push(&buf, &cap, &len, sbuf, c))
			goto oom;
		if (--left == 0)
			goto fail;
		c = scan_getc(s);
		if (c == EOF)
			goto fail;
	}

	if (c == 'i' || c == 'I' || c == 'n' || c == 'N') {
		const char *word = (c == 'i' || c == 'I') ? "infinity"
							 : "nan";
		size_t wlen = strlen(word);
		size_t wpos = 0;	/* word index, past any sign */

		for (;;) {
			if (wpos == wlen) {
				scan_ungetc(s, c);
				break;
			}
			if (tolower(c) != word[wpos]) {
				scan_ungetc(s, c);
				break;
			}
			if (!buf_push(&buf, &cap, &len, sbuf, c))
				goto oom;
			wpos++;
			if (--left == 0)
				break;
			c = scan_getc(s);
			if (c == EOF)
				break;
		}
		/* Only the complete forms are accepted: a partial
		   "infi" or "na" fails the directive like glibc. */
		if (wpos != 3 && wpos != wlen)
			goto fail;
		goto convert;
	}

	if (c == '0') {
		if (!buf_push(&buf, &cap, &len, sbuf, c))
			goto oom;
		if (--left == 0)
			goto convert;
		c = scan_getc(s);
		if (c == 'x' || c == 'X') {
			int c3 = left > 1 ? scan_getc(s) : EOF;

			if (c3 != EOF && isxdigit(c3)) {
				if (!buf_push(&buf, &cap, &len, sbuf, c) ||
				    !buf_push(&buf, &cap, &len, sbuf, c3))
					goto oom;
				left -= 2;
				hex = 1;
				if (left == 0)
					goto convert;
				c = scan_getc(s);
				if (c == EOF)
					goto convert;
			} else {
				/* "0x" without a hex digit fails the
				   directive. */
				scan_ungetc(s, c3);
				scan_ungetc(s, c);
				goto fail;
			}
		}
		if (c == EOF)
			goto convert;
	} else if (!isdigit(c) && c != '.') {
		scan_ungetc(s, c);
		goto fail;
	}

	/* Mantissa: digits with at most one dot. */
	for (;;) {
		if (!(hex ? isxdigit(c) : isdigit(c)) && !(c == '.' && !dot))
			break;
		dot |= (c == '.');
		if (!buf_push(&buf, &cap, &len, sbuf, c))
			goto oom;
		if (--left == 0) {
			c = EOF;
			break;
		}
		c = scan_getc(s);
		if (c == EOF)
			break;
	}

	/* Optional exponent: e/E for decimal, p/P for hex.  An
	   exponent marker that is not completed by digits fails the
	   whole directive, matching glibc. */
	if (c != EOF && left > 0 &&
	    (hex ? (c == 'p' || c == 'P') : (c == 'e' || c == 'E'))) {
		int c2 = scan_getc(s);

		if (c2 == '+' || c2 == '-') {
			int c3 = left > 1 ? scan_getc(s) : EOF;

			if (c3 != EOF && isdigit(c3)) {
				if (!buf_push(&buf, &cap, &len, sbuf, c) ||
				    !buf_push(&buf, &cap, &len, sbuf, c2))
					goto oom;
				left -= 2;
				c2 = c3;
			} else {
				scan_ungetc(s, c3);
				scan_ungetc(s, c2);
				scan_ungetc(s, c);
				goto fail;
			}
		} else if (c2 != EOF && isdigit(c2)) {
			if (!buf_push(&buf, &cap, &len, sbuf, c))
				goto oom;
			left--;
		} else {
			scan_ungetc(s, c2);
			scan_ungetc(s, c);
			goto fail;
		}
		for (;;) {
			if (!buf_push(&buf, &cap, &len, sbuf, c2))
				goto oom;
			if (--left == 0)
				break;
			c2 = scan_getc(s);
			if (c2 == EOF || !isdigit(c2)) {
				scan_ungetc(s, c2);
				break;
			}
		}
	} else if (c != EOF) {
		scan_ungetc(s, c);
	}

convert:
	buf[len] = '\0';
	v = strtod(buf, &end);
	if (end == buf)
		goto fail;
	/* Retract what strtod did not use. */
	while (len-- > (size_t)(end - buf))
		scan_ungetc(s, (unsigned char)buf[len]);
	*out = v;
	if (buf != sbuf)
		free(buf);
	return 1;

oom:
	errno = ENOMEM;
fail:
	while (len > 0)
		scan_ungetc(s, (unsigned char)buf[--len]);
	if (buf != sbuf)
		free(buf);
	return 0;
}

/*
 * Narrow the 64-bit value into the destination type.  Overflow first
 * clamps at the 64-bit boundary like strtol, then the narrowing is
 * modular, matching glibc and musl: 2147483648 read with %d yields
 * INT_MIN, and -1 read with %u yields UINT_MAX.
 */
static void store_int(int len, int unsignedp, int neg,
		      unsigned long long acc, int ov, void *p)
{
	unsigned long long v;

	if (ov)
		v = unsignedp ? ULLONG_MAX
			      : (neg ? (1ULL << 63) : (1ULL << 63) - 1);
	else if (unsignedp)
		v = neg ? 0 - acc : acc;
	else
		v = (unsigned long long)(neg ? -(long long)acc
					     : (long long)acc);

	switch (len) {
	case LEN_HH:
		if (unsignedp)
			*(unsigned char *)p = (unsigned char)v;
		else
			*(signed char *)p = (signed char)v;
		break;
	case LEN_H:
		if (unsignedp)
			*(unsigned short *)p = (unsigned short)v;
		else
			*(short *)p = (short)v;
		break;
	case LEN_NONE:
		if (unsignedp)
			*(unsigned *)p = (unsigned)v;
		else
			*(int *)p = (int)v;
		break;
	case LEN_L:
		if (unsignedp)
			*(unsigned long *)p = (unsigned long)v;
		else
			*(long *)p = (long)v;
		break;
	case LEN_LL:
	case LEN_Z:
	case LEN_J:
	case LEN_T:
		if (unsignedp)
			*(unsigned long long *)p = v;
		else
			*(long long *)p = (long long)v;
		break;
	}
}

int __scan_core(struct scan_src *s, const char *fmt, va_list ap)
{
	int matches = 0;
	int completed = 0;	/* an input directive finished (even
				   suppressed); %n does not count */
	int c;

	for (; *fmt; fmt++) {
		int suppress = 0;
		int width = 0;
		int len = LEN_NONE;
		int conv;

		if (isspace((unsigned char)*fmt)) {
			skip_ws(s);
			continue;
		}
		if (*fmt != '%') {
			c = scan_getc(s);
			if (c == EOF)
				goto input_end;
			if (c != (unsigned char)*fmt) {
				scan_ungetc(s, c);
				goto match_fail;
			}
			continue;
		}

		fmt++;
		if (*fmt == '*') {
			suppress = 1;
			fmt++;
		}
		while (isdigit((unsigned char)*fmt) && width < 100000000)
			width = width * 10 + (*fmt++ - '0');
		switch (*fmt) {
		case 'h':
			fmt++;
			len = *fmt == 'h' ? (fmt++, LEN_HH) : LEN_H;
			break;
		case 'l':
			fmt++;
			len = *fmt == 'l' ? (fmt++, LEN_LL) : LEN_L;
			break;
		case 'z':
			fmt++;
			len = LEN_Z;
			break;
		case 'j':
			fmt++;
			len = LEN_J;
			break;
		case 't':
			fmt++;
			len = LEN_T;
			break;
		case 'L':
			fmt++;
			len = LEN_L;	/* long double is double here */
			break;
		}
		conv = (unsigned char)*fmt;

		switch (conv) {
		case '%':
			c = scan_getc(s);
			if (c == EOF)
				goto input_end;
			if (c != '%') {
				scan_ungetc(s, c);
				goto match_fail;
			}
			break;

		case 'n':
			switch (len) {
			case LEN_HH:
				*(signed char *)va_arg(ap, signed char *) =
					(signed char)s->consumed;
				break;
			case LEN_H:
				*(short *)va_arg(ap, short *) =
					(short)s->consumed;
				break;
			case LEN_NONE:
				*(int *)va_arg(ap, int *) = (int)s->consumed;
				break;
			default:
				*(long *)va_arg(ap, long *) =
					(long)s->consumed;
				break;
			}
			break;

		case 'c': {
			char *dst = suppress ? NULL : va_arg(ap, char *);
			size_t want = width ? (size_t)width : 1;
			size_t n = 0;

			for (; n < want; n++) {
				c = scan_getc(s);
				if (c == EOF) {
					/* The bytes read so far stay
					   written, but a short %c is an
					   input failure, not a match. */
					goto input_end;
				}
				if (dst)
					dst[n] = (char)c;
			}
			completed = 1;
			if (!suppress)
				matches++;
			break;
		}

		case 's': {
			char *dst = suppress ? NULL : va_arg(ap, char *);
			size_t want = width ? (size_t)width : SIZE_MAX;
			size_t n = 0;
			int eof_stop = 0;

			skip_ws(s);
			for (;;) {
				c = scan_getc(s);
				if (c == EOF) {
					eof_stop = 1;
					break;
				}
				if (isspace(c)) {
					scan_ungetc(s, c);
					break;
				}
				if (n >= want) {
					scan_ungetc(s, c);
					break;
				}
				if (dst)
					dst[n] = (char)c;
				n++;
			}
			if (!n) {
				/* Input exhaustion is an input failure;
				   a real character that stops the item is
				   a matching failure. */
				if (eof_stop)
					goto input_end;
				goto match_fail;
			}
			if (dst)
				dst[n] = '\0';
			completed = 1;
			if (!suppress)
				matches++;
			break;
		}

		case '[': {
			unsigned char set[32];
			int invert = 0;
			int prev = -1;
			char *dst = suppress ? NULL : va_arg(ap, char *);
			size_t want = width ? (size_t)width : SIZE_MAX;
			size_t n = 0;

			memset(set, 0, sizeof(set));
			fmt++;
			if (*fmt == '^') {
				invert = 1;
				fmt++;
			}
			if (*fmt == ']') {	/* first ] is literal */
				set[']' >> 3] |= 1u << (']' & 7);
				prev = ']';
				fmt++;
			}
			for (; *fmt && *fmt != ']'; fmt++) {
				int lo = (unsigned char)*fmt;

				if (lo == '-' && prev >= 0 && fmt[1] &&
				    fmt[1] != ']') {
					int hi = (unsigned char)fmt[1];

					for (; prev <= hi; prev++)
						set[prev >> 3] |=
							1u << (prev & 7);
					prev = -1;
					fmt++;	/* the range's high end */
					continue;
				}
				set[lo >> 3] |= 1u << (lo & 7);
				prev = lo;
			}
			if (*fmt != ']') {
				/* unterminated set: stop the scan */
				goto match_fail;
			}

			int eof_stop = 0;

			for (;;) {
				c = scan_getc(s);
				if (c == EOF) {
					eof_stop = 1;
					break;
				}
				{
					int member = !!(set[c >> 3] &
							(1u << (c & 7)));

					if (invert)
						member = !member;
					if (!member) {
						scan_ungetc(s, c);
						break;
					}
				}
				if (n >= want) {
					scan_ungetc(s, c);
					break;
				}
				if (dst)
					dst[n] = (char)c;
				n++;
			}
			if (!n) {
				if (eof_stop)
					goto input_end;
				goto match_fail;
			}
			if (dst)
				dst[n] = '\0';
			completed = 1;
			if (!suppress)
				matches++;
			break;
		}

		case 'd':
		case 'i':
		case 'u':
		case 'o':
		case 'x':
		case 'X': {
			int base = conv == 'i' ? 0
				  : conv == 'o' ? 8
				  : (conv == 'x' || conv == 'X') ? 16 : 10;
			int unsignedp = conv == 'u' || conv == 'o' ||
					conv == 'x' || conv == 'X';
			unsigned long long acc;
			int neg, ov, rc;

			skip_ws(s);
			rc = scan_int(s, width, base, unsignedp, &neg,
				       &acc, &ov);
			if (rc < 0)
				goto input_end;
			if (rc == 0)
				goto match_fail;
			completed = 1;
			if (!suppress) {
				store_int(len, unsignedp, neg, acc, ov,
					  va_arg(ap, void *));
				matches++;
			}
			break;
		}

		case 'f':
		case 'e':
		case 'g':
		case 'a':
		case 'F':
		case 'E':
		case 'G':
		case 'A': {
			double v;
			int rc;

			skip_ws(s);
			rc = scan_float(s, width, &v);
			if (rc < 0)
				goto input_end;
			if (rc == 0)
				goto match_fail;
			completed = 1;
			if (!suppress) {
				if (len == LEN_NONE || len == LEN_HH ||
				    len == LEN_H) {
					float *p = va_arg(ap, float *);

					*p = (float)v;
				} else {
					double *p = va_arg(ap, double *);

					*p = v;
				}
				matches++;
			}
			break;
		}

		default:
			/* Unknown conversion, or an unsupported wide form
			   (ls, lc, l[...): stop with what we have. */
			goto match_fail;
		}
		continue;
	}

	return matches;

input_end:
	return (matches == 0 && !completed) ? EOF : matches;
match_fail:
	return matches;
}
