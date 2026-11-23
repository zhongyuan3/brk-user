#include "brkcc.h"

#include <limits.h>

/*
 * Relocatable object serialization.
 *
 * The writer emits the CCO2 format documented in docs/cc-internal-abi.md.
 * The body of CCO1 is identical; only the magic and version differ, so the
 * reader accepts both without duplicating the record parser.
 */

#define OBJ_MAX_ALIGN 4096
#define OBJ_MAX_BYTES 0x40000000
#define MIN_SYMBOL_RECORD 22 /* u32 + 0-byte name + 4*u32 + 2*u8 */
#define RELOC_RECORD 20

static unsigned int obj_u32(int v, const char *what)
{
	if (v < 0)
		fatal("internal error: %s is negative", what);
	return (unsigned int)v;
}

static unsigned int obj_name_len(const char *name)
{
	size_t n;

	if (!name)
		fatal("internal error: symbol without a name");
	n = strlen(name);
	if (n > (size_t)INT_MAX)
		fatal("internal error: symbol name too long");
	return (unsigned int)n;
}

static void obj_append(struct buf *b, const void *p, int n)
{
	if (n < 0 || b->len < 0 || b->len > OBJ_MAX_BYTES ||
	    n > OBJ_MAX_BYTES - b->len)
		fatal("object file too large");
	buf_grow(b, n);
	if (n)
		memcpy(b->data + b->len, p, (size_t)n);
	b->len += n;
}

static void write_file_bytes(const char *path, const unsigned char *buf,
			     int len, int mode)
{
	int fd;
	int off = 0;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0)
		fatal("cannot open '%s'", path);
	while (off < len) {
		ssize_t n = write(fd, buf + off, (size_t)(len - off));

		if (n <= 0)
			fatal("write failed");
		off += (int)n;
	}
	close(fd);
	chmod(path, mode);
}

static int sym_index(struct sym **arr, int n, struct sym *s)
{
	int i;

	for (i = 0; i < n; i++) {
		if (arr[i] == s)
			return i;
	}
	fatal("internal error: object relocation target is not in symbol table");
	return 0;
}

void write_object(const char *path, struct unit *u)
{
	struct buf b;
	struct sym **arr;
	struct sym *s;
	struct reloc *r;
	int nsyms = 0;
	int ndrels = 0;
	int nrelocs = 0;
	int i = 0;

	if (!u || u->code_len < 0 || u->data_len < 0 || u->bss_len < 0)
		fatal("internal error: invalid unit");

	for (s = u->syms; s; s = s->next) {
		if (nsyms == INT_MAX)
			fatal("too many symbols in object");
		nsyms++;
	}
	for (s = u->syms; s; s = s->next) {
		struct data_reloc *dr;

		for (dr = s->drel; dr; dr = dr->next) {
			if (ndrels == INT_MAX)
				fatal("too many data relocations in object");
			ndrels++;
		}
	}
	for (r = u->relocs; r; r = r->next) {
		if (nrelocs == INT_MAX)
			fatal("too many text relocations in object");
		nrelocs++;
	}

	arr = xcalloc((size_t)(nsyms > 0 ? nsyms : 1), sizeof(*arr));
	for (s = u->syms; s; s = s->next)
		arr[i++] = s;

	buf_init(&b);
	buf_byte(&b, 'C');
	buf_byte(&b, 'C');
	buf_byte(&b, 'O');
	buf_byte(&b, '2');
	buf_u32(&b, 2); /* version */
	buf_u32(&b, obj_u32(u->code_len, "text length"));
	buf_u32(&b, obj_u32(u->data_len, "data length"));
	buf_u32(&b, obj_u32(u->bss_len, "bss length"));
	buf_u32(&b, (unsigned int)nsyms);
	buf_u32(&b, (unsigned int)ndrels);
	buf_u32(&b, (unsigned int)nrelocs);

	obj_append(&b, u->code, u->code_len);
	obj_append(&b, u->data, u->data_len);

	for (i = 0; i < nsyms; i++) {
		struct sym *sym = arr[i];
		unsigned int n;

		if (sym->sec < SEC_TEXT || sym->sec > SEC_BSS)
			fatal("internal error: bad section for symbol '%s'",
			      sym->name);
		if (sym->align <= 0 || sym->align > OBJ_MAX_ALIGN)
			fatal("internal error: bad alignment for symbol '%s'",
			      sym->name);
		if (sym->align & (sym->align - 1))
			fatal("internal error: non-power-of-two alignment for symbol '%s'",
			      sym->name);
		n = obj_name_len(sym->name);
		buf_u32(&b, n);
		obj_append(&b, sym->name, (int)n);
		buf_u32(&b, (unsigned int)sym->sec);
		buf_u32(&b, obj_u32(sym->offset, "symbol offset"));
		buf_u32(&b, obj_u32(sym->size, "symbol size"));
		buf_u32(&b, (unsigned int)sym->align);
		buf_byte(&b, sym->defined ? 1 : 0);
		buf_byte(&b, sym->is_static ? 1 : 0);
	}

	for (i = 0; i < nsyms; i++) {
		struct data_reloc *dr;

		for (dr = arr[i]->drel; dr; dr = dr->next) {
			buf_u32(&b, (unsigned int)i);
			buf_u32(&b,
				obj_u32(dr->offset, "data relocation offset"));
			buf_u32(&b,
				(unsigned int)sym_index(arr, nsyms, dr->sym));
			buf_u64(&b, (unsigned long)dr->addend);
		}
	}
	for (r = u->relocs; r; r = r->next) {
		if (r->kind < R_HI20 || r->kind > R_JAL)
			fatal("internal error: bad relocation kind");
		buf_u32(&b, obj_u32(r->pos, "relocation offset"));
		buf_u32(&b, (unsigned int)r->kind);
		buf_u32(&b, (unsigned int)sym_index(arr, nsyms, r->sym));
		buf_u64(&b, (unsigned long)r->addend);
	}

	write_file_bytes(path, b.data, b.len, 0644);
	free(arr);
}

/* ------------------------------------------------------------------ */
/* reader                                                              */
/* ------------------------------------------------------------------ */

struct rd {
	const unsigned char *p;
	int len;
	int pos;
	const char *path;
};

static void rd_need(struct rd *r, int n)
{
	if (n < 0 || r->pos < 0 || r->pos > r->len || n > r->len - r->pos)
		fatal("truncated object file '%s'", r->path);
}

static unsigned char rd_u8(struct rd *r)
{
	rd_need(r, 1);
	return r->p[r->pos++];
}

static unsigned int rd_u32(struct rd *r)
{
	unsigned int v;

	rd_need(r, 4);
	v = (unsigned int)r->p[r->pos] | ((unsigned int)r->p[r->pos + 1] << 8) |
	    ((unsigned int)r->p[r->pos + 2] << 16) |
	    ((unsigned int)r->p[r->pos + 3] << 24);
	r->pos += 4;
	return v;
}

static unsigned long rd_u64(struct rd *r)
{
	unsigned long v = 0;
	int i;

	rd_need(r, 8);
	for (i = 0; i < 8; i++)
		v |= (unsigned long)r->p[r->pos + i] << (i * 8);
	r->pos += 8;
	return v;
}

static void rd_bytes(struct rd *r, void *dst, int n)
{
	rd_need(r, n);
	if (n)
		memcpy(dst, r->p + r->pos, (size_t)n);
	r->pos += n;
}

static int rd_len32(struct rd *r, const char *what)
{
	unsigned int v = rd_u32(r);

	if (v > (unsigned int)INT_MAX)
		fatal("%s too large in object file '%s'", what, r->path);
	return (int)v;
}

static int rd_flag(struct rd *r, const char *what)
{
	unsigned int v = rd_u8(r);

	if (v > 1)
		fatal("invalid %s in object file '%s'", what, r->path);
	return (int)v;
}

static char *rd_name(struct rd *r)
{
	unsigned int n = rd_u32(r);
	char *s;

	if (n > (unsigned int)INT_MAX)
		fatal("symbol name too long in object file '%s'", r->path);
	rd_need(r, (int)n);
	s = xmalloc((size_t)n + 1);
	memcpy(s, r->p + r->pos, (size_t)n);
	s[n] = '\0';
	if (memchr(s, '\0', (size_t)n))
		fatal("invalid symbol name in object file '%s'", r->path);
	r->pos += (int)n;
	return s;
}

static void check_symbol(const char *path, struct unit *u, struct sym *s)
{
	if (s->sec < SEC_TEXT || s->sec > SEC_BSS)
		fatal("bad section for symbol '%s' in object file '%s'",
		      s->name, path);
	if (s->align <= 0 || s->align > OBJ_MAX_ALIGN)
		fatal("bad alignment for symbol '%s' in object file '%s'",
		      s->name, path);
	if (s->align & (s->align - 1))
		fatal("bad alignment for symbol '%s' in object file '%s'",
		      s->name, path);
	if (!s->defined)
		return;
	if (s->offset < 0 || s->size < 0)
		fatal("bad extent for symbol '%s' in object file '%s'",
		      s->name, path);
	if (s->offset % s->align)
		fatal("misaligned symbol '%s' in object file '%s'", s->name,
		      path);
	if (s->sec == SEC_TEXT) {
		if (s->offset % 4)
			fatal("misaligned text symbol '%s' in object file '%s'",
			      s->name, path);
		if (s->offset > u->code_len ||
		    s->size > u->code_len - s->offset)
			fatal("symbol '%s' outside text section in object file '%s'",
			      s->name, path);
	} else if (s->sec == SEC_DATA) {
		if (s->offset > u->data_len ||
		    s->size > u->data_len - s->offset)
			fatal("symbol '%s' outside data section in object file '%s'",
			      s->name, path);
	} else {
		if (s->offset > u->bss_len ||
		    s->size > u->bss_len - s->offset)
			fatal("symbol '%s' outside bss section in object file '%s'",
			      s->name, path);
	}
}

static void check_count(const char *path, const char *what, unsigned int n,
			unsigned int min_size, int avail)
{
	if (avail < 0 || n > (unsigned int)(avail / (int)min_size))
		fatal("truncated object file '%s' (bad %s count)", path, what);
}

struct unit *read_object(const char *path)
{
	struct unit *u;
	struct sym **arr;
	struct sym *tail = NULL;
	char *buf;
	int len;
	struct rd rd;
	unsigned int version;
	unsigned int nsyms;
	unsigned int ndrels;
	unsigned int nrelocs;
	int i;

	if (try_read_file(path, &buf, &len) < 0)
		fatal("cannot open object file '%s'", path);
	if (len < 0 || len > OBJ_MAX_BYTES)
		fatal("object file too large '%s'", path);
	memset(&rd, 0, sizeof(rd));
	rd.p = (const unsigned char *)buf;
	rd.len = len;
	rd.path = path;

	{
		char magic[4];
		unsigned int expected;

		rd_bytes(&rd, magic, 4);
		if (!memcmp(magic, "CCO1", 4)) {
			expected = 1;
		} else if (!memcmp(magic, "CCO2", 4)) {
			expected = 2;
		} else {
			fatal("'%s' is not a cc object file", path);
		}
		version = rd_u32(&rd);
		if (version != expected)
			fatal("unsupported object file version %u in '%s' (expected %u)",
			      version, path, expected);
	}

	u = xcalloc(1, sizeof(*u));
	u->code_len = rd_len32(&rd, "text section");
	u->data_len = rd_len32(&rd, "data section");
	u->bss_len = rd_len32(&rd, "bss section");
	nsyms = rd_u32(&rd);
	ndrels = rd_u32(&rd);
	nrelocs = rd_u32(&rd);

	if ((unsigned long)u->code_len + (unsigned long)u->data_len >
	    (unsigned long)(rd.len - rd.pos))
		fatal("truncated object file '%s'", path);

	u->code = xmalloc((size_t)(u->code_len > 0 ? u->code_len : 1));
	rd_bytes(&rd, u->code, u->code_len);
	u->data = xmalloc((size_t)(u->data_len > 0 ? u->data_len : 1));
	rd_bytes(&rd, u->data, u->data_len);

	check_count(path, "symbol", nsyms, MIN_SYMBOL_RECORD,
		    rd.len - rd.pos);
	check_count(path, "data relocation", ndrels, RELOC_RECORD,
		    rd.len - rd.pos);
	check_count(path, "text relocation", nrelocs, RELOC_RECORD,
		    rd.len - rd.pos);

	arr = xcalloc((size_t)(nsyms > 0 ? nsyms : 1), sizeof(*arr));
	for (i = 0; i < (int)nsyms; i++) {
		struct sym *s = xcalloc(1, sizeof(*s));

		s->name = rd_name(&rd);
		s->sec = (int)rd_u32(&rd);
		s->offset = rd_len32(&rd, "symbol offset");
		s->size = rd_len32(&rd, "symbol size");
		s->align = (int)rd_u32(&rd);
		s->defined = rd_flag(&rd, "symbol defined flag") != 0;
		s->is_static = rd_flag(&rd, "symbol static flag") != 0;
		check_symbol(path, u, s);
		arr[i] = s;
		if (tail)
			tail->next = s;
		else
			u->syms = s;
		tail = s;
	}

	for (i = 0; i < (int)ndrels; i++) {
		unsigned int owner = rd_u32(&rd);
		int off = rd_len32(&rd, "data relocation offset");
		unsigned int target = rd_u32(&rd);
		unsigned long addend = rd_u64(&rd);
		struct data_reloc *dr;
		struct sym *own;

		if (owner >= nsyms || target >= nsyms)
			fatal("bad symbol index in object file '%s'", path);
		own = arr[owner];
		if (!own->defined || own->sec != SEC_DATA)
			fatal("data relocation owner '%s' is not a data definition in object file '%s'",
			      own->name, path);
		if (own->size < 8 || off > own->size - 8)
			fatal("data relocation out of range for symbol '%s' in object file '%s'",
			      own->name, path);
		dr = xcalloc(1, sizeof(*dr));
		dr->offset = off;
		dr->sym = arr[target];
		dr->addend = (long)addend;
		dr->next = own->drel;
		own->drel = dr;
	}
	for (i = 0; i < (int)nrelocs; i++) {
		int pos = rd_len32(&rd, "text relocation offset");
		unsigned int kind = rd_u32(&rd);
		unsigned int target = rd_u32(&rd);
		unsigned long addend = rd_u64(&rd);
		struct reloc *r;

		if (kind > R_JAL)
			fatal("bad relocation kind %u in object file '%s'", kind,
			      path);
		if (target >= nsyms)
			fatal("bad symbol index in object file '%s'", path);
		if (pos % 4 || pos > u->code_len - 4)
			fatal("text relocation out of range in object file '%s'",
			      path);
		r = xcalloc(1, sizeof(*r));
		r->pos = pos;
		r->kind = (int)kind;
		r->sym = arr[target];
		r->addend = (long)addend;
		r->next = u->relocs;
		u->relocs = r;
	}
	if (rd.pos != rd.len)
		fatal("trailing data in object file '%s'", path);

	free(arr);
	free(buf);
	if (unit_tail)
		unit_tail->next = u;
	else
		units = u;
	unit_tail = u;
	return u;
}
