#include "cc.h"

void fatal(const char *fmt, ...)
	__attribute__((noreturn, format(printf, 1, 2)));

void fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "cc: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);

	if (!p)
		fatal("out of memory");
	return p;
}

void *xcalloc(size_t n, size_t sz)
{
	void *p = calloc(n ? n : 1, sz ? sz : 1);

	if (!p)
		fatal("out of memory");
	return p;
}

void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);

	if (!q)
		fatal("out of memory");
	return q;
}

char *xstrndup(const char *s, size_t n)
{
	char *p = xmalloc(n + 1);

	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}

char *xstrdup(const char *s)
{
	return xstrndup(s, strlen(s));
}

int align_to(int n, int a)
{
	return (n + a - 1) / a * a;
}

void buf_init(struct buf *b)
{
	b->len = 0;
	b->cap = 256;
	b->data = xmalloc((size_t)b->cap);
}

void buf_grow(struct buf *b, int need)
{
	if (b->len + need <= b->cap)
		return;
	while (b->cap < b->len + need)
		b->cap *= 2;
	b->data = xrealloc(b->data, (size_t)b->cap);
}

void buf_byte(struct buf *b, int c)
{
	buf_grow(b, 1);
	b->data[b->len++] = (unsigned char)c;
}

void buf_u32(struct buf *b, unsigned int v)
{
	buf_byte(b, (int)(v & 0xff));
	buf_byte(b, (int)((v >> 8) & 0xff));
	buf_byte(b, (int)((v >> 16) & 0xff));
	buf_byte(b, (int)((v >> 24) & 0xff));
}

void buf_u64(struct buf *b, unsigned long v)
{
	int i;

	for (i = 0; i < 8; i++)
		buf_byte(b, (int)((v >> (i * 8)) & 0xff));
}

void buf_pad(struct buf *b, int n)
{
	while (n-- > 0)
		buf_byte(b, 0);
}

int buf_align(struct buf *b, int a)
{
	int pad = align_to(b->len, a) - b->len;

	buf_pad(b, pad);
	return pad;
}
