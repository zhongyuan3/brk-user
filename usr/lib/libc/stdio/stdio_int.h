#ifndef _STDIO_INT_H
#define _STDIO_INT_H

#include <stdio.h>

#include "stdio_impl.h"

int __stdio_file_write(FILE *stream, char const *buf, size_t len, size_t *wlen);
int __stdio_flush_wbuf(FILE *stream);
int __stdio_purge_readbuf(FILE *stream);

/*
 * Shared printf plumbing: the integer/pointer engine in vfprintf.c and
 * the floating-point engine in fmt_fp.c both emit through these.
 */
struct fmt_context {
	FILE *stream;
	size_t cnt;
};

/* Flag bits shared between the printf engines. */
#define PRTF_ZERO_PAD (1U << 0)
#define PRTF_LEFT_ALIGN (1U << 1)
#define PRTF_MARK_POS (1U << 2)
#define PRTF_ALT_FORM (1U << 3)
#define PRTF_PAD_POS (1U << 4)

void __printf_out(struct fmt_context *ctx, char const *buf, size_t len);
void __printf_pad(struct fmt_context *ctx, size_t pad_len, char pad_ch);

/*
 * Correctly rounded %e/%f/%g/%a rendering (the conversion character is
 * conv, with the case significant).  width is 0 when unspecified;
 * prec is -1 when unspecified.  Returns the number of characters
 * emitted, or -1 on overflow.
 */
int __printf_fmt_fp(struct fmt_context *ctx, double y, size_t width,
		    int prec, unsigned int flags, int conv);

#endif
