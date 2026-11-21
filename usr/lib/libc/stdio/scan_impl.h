#ifndef _SCAN_IMPL_H
#define _SCAN_IMPL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

/*
 * Character source for the scanf engine: a FILE stream or a string,
 * plus a LIFO of retracted characters.  Every conversion only ever
 * needs to retract a bounded speculative tail — a stray 'x' or 'e+'
 * after a number, a partial "inf" — so the ring is fixed size.
 */
#define SCAN_PENDING 32

struct scan_src {
	FILE *fp;			/* NULL: the string source below */
	const char *str;			/* next character of the string */
	unsigned char pending[SCAN_PENDING];
	unsigned pending_len;		/* retracted, served first */
	size_t consumed;		/* characters kept, for %n */
};

static inline int scan_getc(struct scan_src *s)
{
	int c;

	if (s->pending_len) {
		c = s->pending[--s->pending_len];
	} else if (s->fp) {
		c = fgetc(s->fp);
		if (c == EOF)
			return EOF;
	} else {
		c = (unsigned char)*s->str;
		if (!c)
			return EOF;
		s->str++;
	}
	s->consumed++;
	return c;
}

/* Retract one character; EOF is a no-op (end of input stays sticky). */
static inline void scan_ungetc(struct scan_src *s, int c)
{
	if (c == EOF)
		return;
	if (s->pending_len < SCAN_PENDING) {
		s->pending[s->pending_len++] = (unsigned char)c;
		s->consumed--;
	}
}

/*
 * The engine: vfscanf/vsscanf bodies.  Returns the number of
 * assigned conversions, or EOF on input exhaustion before the first
 * conversion completes.
 */
int __scan_core(struct scan_src *s, const char *fmt, va_list ap);

#endif
