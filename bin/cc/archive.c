#include "brkcc.h"

#include <limits.h>

/*
 * Minimal GNU ar reader.  Archives are used only as link inputs: the symbol
 * index selects a member, and the member is parsed as an ELF ET_REL object.
 */

struct ar_member {
	char *name;
	unsigned long hdr_off;
	unsigned long data_off;
	unsigned long size;
	bool loaded;
};

struct ar_symbol {
	char *name;
	int member;
};

struct archive {
	char *path;
	unsigned char *data;
	int len;
	struct ar_member *members;
	int nmembers;
	struct ar_symbol *syms;
	int nsyms;
};

static unsigned long ar_be32(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
	       ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static unsigned long ar_decimal(const unsigned char *p, int n,
				const char *path)
{
	unsigned long v = 0;
	int i = 0;

	while (i < n && p[i] == ' ')
		i++;
	if (i == n)
		return 0;
	for (; i < n; i++) {
		if (p[i] == ' ')
			break;
		if (p[i] < '0' || p[i] > '9')
			fatal("bad number in archive '%s'", path);
		if (v > ((unsigned long)-1 - (unsigned long)(p[i] - '0')) / 10)
			fatal("archive member too large in '%s'", path);
		v = v * 10 + (unsigned long)(p[i] - '0');
	}
	return v;
}

static char *ar_name16(const unsigned char *p, const char *path)
{
	char tmp[17];
	int n = 16;

	memcpy(tmp, p, 16);
	tmp[16] = '\0';
	while (n > 0 && tmp[n - 1] == ' ')
		tmp[--n] = '\0';
	if (n == 0)
		fatal("empty member name in archive '%s'", path);
	return xstrdup(tmp);
}

static void ar_add_member(struct archive *a, char *name, unsigned long hdr_off,
			  unsigned long data_off, unsigned long size)
{
	if (a->nmembers == INT_MAX)
		fatal("too many members in archive '%s'", a->path);
	a->members = xrealloc(a->members,
			      (size_t)(a->nmembers + 1) *
				      sizeof(*a->members));
	a->members[a->nmembers].name = name;
	a->members[a->nmembers].hdr_off = hdr_off;
	a->members[a->nmembers].data_off = data_off;
	a->members[a->nmembers].size = size;
	a->members[a->nmembers].loaded = false;
	a->nmembers++;
}

static void ar_add_symbol(struct archive *a, const char *name, int member)
{
	if (!*name)
		return;
	if (a->nsyms == INT_MAX)
		fatal("too many symbols in archive '%s'", a->path);
	a->syms = xrealloc(a->syms,
			   (size_t)(a->nsyms + 1) * sizeof(*a->syms));
	a->syms[a->nsyms].name = xstrdup(name);
	a->syms[a->nsyms].member = member;
	a->nsyms++;
}

static int ar_find_member_by_off(struct archive *a, unsigned long off)
{
	int i;

	for (i = 0; i < a->nmembers; i++) {
		if (a->members[i].hdr_off == off)
			return i;
	}
	return -1;
}

struct archive *open_archive(const char *path)
{
	struct archive *a;
	char *buf;
	int len;
	unsigned long pos;
	unsigned long index_off = 0;
	unsigned long index_size = 0;
	const unsigned char *longnames = NULL;
	unsigned long longnames_size = 0;
	int i;

	if (try_read_file(path, &buf, &len) < 0)
		return NULL;
	if (len < 8 || memcmp(buf, "!<arch>\n", 8))
		fatal("'%s' is not an archive", path);

	a = xcalloc(1, sizeof(*a));
	a->path = xstrdup(path);
	a->data = (unsigned char *)buf;
	a->len = len;

	pos = 8;
	while (pos < (unsigned long)len) {
		const unsigned char *h;
		unsigned long size;
		unsigned long data_off;
		char *name;

		if ((unsigned long)len - pos < 60)
			fatal("truncated archive '%s'", path);
		h = a->data + pos;
		if (h[58] != '`' || h[59] != '\n')
			fatal("bad member header in archive '%s'", path);
		size = ar_decimal(h + 48, 10, path);
		data_off = pos + 60;
		if (size > (unsigned long)len - data_off)
			fatal("truncated archive member in '%s'", path);

		name = ar_name16(h, path);
		if (!strcmp(name, "/")) {
			free(name);
			index_off = data_off;
			index_size = size;
		} else if (!strcmp(name, "//")) {
			free(name);
			longnames = a->data + data_off;
			longnames_size = size;
		} else if (name[0] == '/' && name[1] >= '0' &&
			   name[1] <= '9') {
			unsigned long off = 0;
			const char *p = name + 1;
			unsigned long n;
			char *resolved;

			while (*p >= '0' && *p <= '9') {
				off = off * 10 + (unsigned long)(*p - '0');
				p++;
			}
			if (off >= longnames_size)
				fatal("bad long member name in archive '%s'",
				      path);
			n = 0;
			while (off + n < longnames_size &&
			       longnames[off + n] != '/' &&
			       longnames[off + n] != '\n')
				n++;
			if (n == 0 || off + n >= longnames_size)
				fatal("unterminated long member name in archive '%s'",
				      path);
			resolved = xmalloc((size_t)n + 1);
			memcpy(resolved, longnames + off, (size_t)n);
			resolved[n] = '\0';
			free(name);
			name = resolved;
			ar_add_member(a, name, pos, data_off, size);
		} else {
			size_t n = strlen(name);

			if (n > 1 && name[n - 1] == '/')
				name[n - 1] = '\0';
			ar_add_member(a, name, pos, data_off, size);
		}

		pos = data_off + size;
		if (size & 1)
			pos++;
	}

	if (index_size) {
		unsigned long count;
		unsigned long names_off;
		const unsigned char *p;

		if (index_size < 4)
			fatal("bad symbol index in archive '%s'", path);
		p = a->data + index_off;
		count = ar_be32(p);
		if (count > (index_size - 4) / 4)
			fatal("bad symbol index in archive '%s'", path);
		names_off = 4 + count * 4;
		p = a->data + index_off + names_off;
		for (i = 0; i < (int)count; i++) {
			unsigned long off = ar_be32(a->data + index_off + 4 +
						    (unsigned long)i * 4);
			unsigned long hdr;
			size_t n = 0;
			int member;

			while (index_off + names_off + n < index_off + index_size &&
			       p[n] != '\0')
				n++;
			if (index_off + names_off + n >=
			    index_off + index_size)
				fatal("bad symbol index in archive '%s'", path);
			hdr = off;
			member = ar_find_member_by_off(a, hdr);
			if (member >= 0)
				ar_add_symbol(a, (const char *)p, member);
			p += n + 1;
			names_off += n + 1;
		}
	}

	return a;
}

static int ar_symbol_member(struct archive *a, const char *name)
{
	int i;

	for (i = 0; i < a->nsyms; i++) {
		if (!strcmp(a->syms[i].name, name) &&
		    !a->members[a->syms[i].member].loaded)
			return a->syms[i].member;
	}
	return -1;
}

bool archive_has_symbol(struct archive *a, const char *name)
{
	return ar_symbol_member(a, name) >= 0;
}

bool archive_load_symbol(struct archive *a, const char *name)
{
	int m = ar_symbol_member(a, name);
	struct ar_member *mem;
	char *display;

	if (m < 0)
		return false;
	mem = &a->members[m];
	if (mem->data_off > (unsigned long)a->len ||
	    mem->size > (unsigned long)a->len - mem->data_off)
		fatal("truncated archive member in '%s'", a->path);
	display = xmalloc(strlen(a->path) + strlen(mem->name) + 3);
	strcpy(display, a->path);
	strcat(display, "(");
	strcat(display, mem->name);
	strcat(display, ")");
	(void)read_elf_object(display, a->data + mem->data_off,
			      (int)mem->size);
	free(display);
	mem->loaded = true;
	return true;
}
