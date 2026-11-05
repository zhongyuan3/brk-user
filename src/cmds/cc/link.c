#include "cc.h"

/* ------------------------------------------------------------------ */
/* ELF writer                                                          */
/* ------------------------------------------------------------------ */

#define USER_BASE 0x10000UL
#define TEXT_FILE_OFF 0x1000
#define EM_RISCV 243

static void put16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void put64(unsigned char *p, unsigned long v)
{
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (unsigned char)((v >> (i * 8)) & 0xff);
}

struct link_sym {
	char *name;
	struct sym *sym;
	struct link_sym *next;
};

static struct link_sym *link_syms;

static struct link_sym *link_find(const char *name)
{
	struct link_sym *l;

	for (l = link_syms; l; l = l->next) {
		if (!strcmp(l->name, name))
			return l;
	}
	return NULL;
}

static void link_define(struct sym *s)
{
	struct link_sym *l;

	if (!s->defined || s->is_static)
		return;
	if (link_find(s->name))
		fatal("duplicate symbol '%s'", s->name);
	l = xcalloc(1, sizeof(*l));
	l->name = xstrdup(s->name);
	l->sym = s;
	l->next = link_syms;
	link_syms = l;
}

static unsigned long resolve_sym(struct sym *s)
{
	struct link_sym *l;

	if (s->defined)
		return s->addr;
	l = link_find(s->name);
	if (!l)
		fatal("undefined symbol '%s'", s->name);
	return l->sym->addr;
}

static void put_ins(unsigned char *buf, int pos, unsigned int ins)
{
	buf[pos + 0] = (unsigned char)(ins & 0xff);
	buf[pos + 1] = (unsigned char)((ins >> 8) & 0xff);
	buf[pos + 2] = (unsigned char)((ins >> 16) & 0xff);
	buf[pos + 3] = (unsigned char)((ins >> 24) & 0xff);
}

void finalize_unit(void)
{
	struct unit *u = xcalloc(1, sizeof(*u));
	struct sym *s;
	struct buf data;
	int off;

	u->code_len = code.len;
	u->code = xmalloc((size_t)(code.len > 0 ? code.len : 1));
	if (code.len > 0)
		memcpy(u->code, code.data, (size_t)code.len);
	u->syms = syms;
	u->relocs = relocs;

	off = 0;
	for (s = syms; s; s = s->next) {
		if (!s->defined || s->sec != SEC_DATA)
			continue;
		off = align_to(off, s->align);
		s->offset = off;
		off += s->size;
	}

	buf_init(&data);
	for (s = syms; s; s = s->next) {
		if (!s->defined || s->sec != SEC_DATA)
			continue;
		buf_align(&data, s->align);
		if (s->init && s->size > 0) {
			buf_grow(&data, s->size);
			memcpy(data.data + data.len, s->init, (size_t)s->size);
			data.len += s->size;
		} else {
			buf_pad(&data, s->size);
		}
	}
	u->data = data.data;
	u->data_len = data.len;

	off = 0;
	for (s = syms; s; s = s->next) {
		if (!s->defined || s->sec != SEC_BSS)
			continue;
		off = align_to(off, s->align);
		s->offset = off;
		off += s->size;
	}
	u->bss_len = off;

	if (unit_tail)
		unit_tail->next = u;
	else
		units = u;
	unit_tail = u;
}

static void patch_unit_relocs(struct unit *u, unsigned long text_section_base)
{
	struct reloc *r;
	struct sym *s;

	for (r = u->relocs; r; r = r->next) {
		unsigned long target =
			resolve_sym(r->sym) + (unsigned long)r->addend;
		unsigned long pos_abs = text_section_base + u->text_base +
					(unsigned long)r->pos;
		unsigned int old;
		unsigned int ins;

		memcpy(&old, u->code + r->pos, 4);
		switch (r->kind) {
		case R_HI20: {
			long hi = (long)((target + 0x800) >> 12);

			ins = enc_u(0x37, (int)rd_of(old), (int)hi);
			break;
		}
		case R_LO12I: {
			long lo = (long)(target & 0xfff);

			if (lo & 0x800)
				lo -= 0x1000;
			ins = enc_i(0x13, (int)rd_of(old), 0, (int)rs1_of(old),
				    (int)lo);
			break;
		}
		case R_JAL: {
			long off = (long)target - (long)pos_abs;

			if (off & 1)
				fatal("internal error: unaligned call");
			if (off < -(1 << 20) || off >= (1 << 20))
				fatal("program too large for direct call");
			ins = enc_j(0x6f, (int)rd_of(old), (int)off);
			break;
		}
		default:
			fatal("internal error: bad relocation");
			return;
		}
		put_ins(u->code, r->pos, ins);
	}

	for (s = u->syms; s; s = s->next) {
		struct data_reloc *dr;

		if (!s->defined || s->sec != SEC_DATA)
			continue;
		for (dr = s->drel; dr; dr = dr->next) {
			unsigned long target = resolve_sym(dr->sym) +
					       (unsigned long)dr->addend;
			int at = s->offset + dr->offset;

			if (at + 8 > u->data_len)
				fatal("internal error: data relocation overflow");
			put64(u->data + at, target);
		}
	}
}

static struct sym *find_main_sym(void)
{
	struct sym *found = NULL;
	struct unit *u;

	for (u = units; u; u = u->next) {
		struct sym *s;

		for (s = u->syms; s; s = s->next) {
			if (!s->defined || s->is_static || s->sec != SEC_TEXT)
				continue;
			if (!strcmp(s->name, "main")) {
				if (found)
					fatal("multiple definitions of 'main'");
				found = s;
			}
		}
	}
	return found;
}

static void ensure_runtime_unit(void)
{
	reset_emit_state();
	reset_codegen_state();
	gen_runtime();
	finalize_unit();
}

static void ensure_start_unit(void)
{
	struct sym *main_sym = find_main_sym();

	if (!main_sym)
		fatal("no definition of 'main'");
	reset_emit_state();
	reset_codegen_state();
	gen_start(main_sym);
	finalize_unit();
}

void link_and_write(const char *path)
{
	struct unit *u;
	struct sym *s;
	struct link_sym *l;
	unsigned long text_off = 0;
	unsigned long data_off = 0;
	unsigned long bss_off = 0;
	unsigned long text_section_base;
	unsigned long data_section_base;
	unsigned long bss_section_base;
	unsigned long file_len;
	unsigned long mem_len;
	unsigned long entry;
	unsigned char *text;
	unsigned char *data;
	unsigned char *file;
	int data_file_off;
	int fd;
	int written;

	ensure_runtime_unit();
	ensure_start_unit();

	link_syms = NULL;
	for (u = units; u; u = u->next) {
		text_off = (unsigned long)align_to((int)text_off, 16);
		u->text_base = text_off;
		text_off += (unsigned long)u->code_len;
	}
	for (u = units; u; u = u->next) {
		data_off = (unsigned long)align_to((int)data_off, 16);
		u->data_base = data_off;
		data_off += (unsigned long)u->data_len;
	}
	for (u = units; u; u = u->next) {
		bss_off = (unsigned long)align_to((int)bss_off, 16);
		u->bss_base = bss_off;
		bss_off += (unsigned long)u->bss_len;
	}

	text_section_base = USER_BASE + TEXT_FILE_OFF;
	data_section_base = (unsigned long)align_to(
		(int)(text_section_base + text_off), 16);
	bss_section_base = (unsigned long)align_to(
		(int)(data_section_base + data_off), 16);

	for (u = units; u; u = u->next) {
		for (s = u->syms; s; s = s->next)
			link_define(s);
	}
	for (u = units; u; u = u->next) {
		for (s = u->syms; s; s = s->next) {
			if (!s->defined)
				continue;
			if (s->sec == SEC_TEXT)
				s->addr = text_section_base + u->text_base +
					  (unsigned long)s->offset;
			else if (s->sec == SEC_DATA)
				s->addr = data_section_base + u->data_base +
					  (unsigned long)s->offset;
			else
				s->addr = bss_section_base + u->bss_base +
					  (unsigned long)s->offset;
		}
	}
	for (u = units; u; u = u->next)
		patch_unit_relocs(u, text_section_base);

	l = link_find("__cc_start");
	if (!l)
		fatal("no definition of 'main'");
	entry = l->sym->addr;

	text = xcalloc(1, text_off ? (size_t)text_off : 1);
	for (u = units; u; u = u->next) {
		if (u->code_len)
			memcpy(text + u->text_base, u->code,
			       (size_t)u->code_len);
	}
	data = xcalloc(1, data_off ? (size_t)data_off : 1);
	for (u = units; u; u = u->next) {
		if (u->data_len)
			memcpy(data + u->data_base, u->data,
			       (size_t)u->data_len);
	}

	data_file_off = (int)(data_section_base - USER_BASE);
	file_len = (unsigned long)data_file_off + data_off;
	mem_len = (bss_section_base - USER_BASE) + bss_off;
	file = xcalloc(1, (size_t)file_len);
	memcpy(file + TEXT_FILE_OFF, text, (size_t)text_off);
	if (data_off)
		memcpy(file + data_file_off, data, (size_t)data_off);

	/* ELF header */
	file[0] = 0x7f;
	file[1] = 'E';
	file[2] = 'L';
	file[3] = 'F';
	file[4] = 2; /* ELFCLASS64 */
	file[5] = 1; /* little endian */
	file[6] = 1; /* version */
	put16(file + 16, 2); /* ET_EXEC */
	put16(file + 18, EM_RISCV); /* e_machine */
	put32(file + 20, 1); /* e_version */
	put64(file + 24, entry);
	put64(file + 32, 64); /* e_phoff */
	put64(file + 40, 0); /* e_shoff */
	put32(file + 48, 0); /* e_flags */
	put16(file + 52, 64); /* e_ehsize */
	put16(file + 54, 56); /* e_phentsize */
	put16(file + 56, 1); /* e_phnum */
	put16(file + 58, 0); /* e_shentsize */
	put16(file + 60, 0); /* e_shnum */
	put16(file + 62, 0); /* e_shstrndx */

	/* program header: one RWX PT_LOAD */
	put32(file + 64, 1); /* PT_LOAD */
	put32(file + 68, 7); /* R|W|X */
	put64(file + 72, 0); /* p_offset */
	put64(file + 80, USER_BASE);
	put64(file + 88, USER_BASE);
	put64(file + 96, file_len);
	put64(file + 104, mem_len > file_len ? mem_len : file_len);
	put64(file + 112, 0x1000);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (fd < 0)
		fatal("cannot open output file '%s'", path);
	written = 0;
	while (written < (int)file_len) {
		ssize_t n = write(fd, file + written,
				  (size_t)((int)file_len - written));

		if (n <= 0)
			fatal("write failed");
		written += (int)n;
	}
	close(fd);
	chmod(path, 0755);
	free(file);
	free(text);
	free(data);
}

struct rd {
	const unsigned char *p;
	int len;
	int pos;
};

static unsigned char rd_u8(struct rd *r)
{
	if (r->pos + 1 > r->len)
		fatal("truncated object file");
	return r->p[r->pos++];
}

static unsigned int rd_u32(struct rd *r)
{
	unsigned int v;

	if (r->pos + 4 > r->len)
		fatal("truncated object file");
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

	if (r->pos + 8 > r->len)
		fatal("truncated object file");
	for (i = 0; i < 8; i++)
		v |= (unsigned long)r->p[r->pos + i] << (i * 8);
	r->pos += 8;
	return v;
}

static void rd_bytes(struct rd *r, void *dst, int n)
{
	if (n < 0 || r->pos + n > r->len)
		fatal("truncated object file");
	memcpy(dst, r->p + r->pos, (size_t)n);
	r->pos += n;
}

static char *rd_name(struct rd *r)
{
	unsigned int n = rd_u32(r);
	char *s;

	if ((int)n < 0 || r->pos + (int)n > r->len)
		fatal("truncated object file");
	s = xmalloc((size_t)n + 1);
	memcpy(s, r->p + r->pos, n);
	s[n] = '\0';
	r->pos += (int)n;
	return s;
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

	for (s = u->syms; s; s = s->next)
		nsyms++;
	for (s = u->syms; s; s = s->next) {
		struct data_reloc *dr;

		for (dr = s->drel; dr; dr = dr->next)
			ndrels++;
	}
	for (r = u->relocs; r; r = r->next)
		nrelocs++;

	arr = xcalloc((size_t)(nsyms > 0 ? nsyms : 1), sizeof(*arr));
	for (s = u->syms; s; s = s->next)
		arr[i++] = s;

	buf_init(&b);
	buf_byte(&b, 'C');
	buf_byte(&b, 'C');
	buf_byte(&b, 'O');
	buf_byte(&b, '1');
	buf_u32(&b, 1); /* version */
	buf_u32(&b, (unsigned int)u->code_len);
	buf_u32(&b, (unsigned int)u->data_len);
	buf_u32(&b, (unsigned int)u->bss_len);
	buf_u32(&b, (unsigned int)nsyms);
	buf_u32(&b, (unsigned int)ndrels);
	buf_u32(&b, (unsigned int)nrelocs);

	buf_grow(&b, u->code_len);
	memcpy(b.data + b.len, u->code, (size_t)u->code_len);
	b.len += u->code_len;
	buf_grow(&b, u->data_len);
	memcpy(b.data + b.len, u->data, (size_t)u->data_len);
	b.len += u->data_len;

	for (i = 0; i < nsyms; i++) {
		int n = (int)strlen(arr[i]->name);

		buf_u32(&b, (unsigned int)n);
		buf_grow(&b, n);
		memcpy(b.data + b.len, arr[i]->name, (size_t)n);
		b.len += n;
		buf_u32(&b, (unsigned int)arr[i]->sec);
		buf_u32(&b, (unsigned int)arr[i]->offset);
		buf_u32(&b, (unsigned int)arr[i]->size);
		buf_u32(&b, (unsigned int)arr[i]->align);
		buf_byte(&b, arr[i]->defined ? 1 : 0);
		buf_byte(&b, arr[i]->is_static ? 1 : 0);
	}

	for (i = 0; i < nsyms; i++) {
		struct data_reloc *dr;

		for (dr = arr[i]->drel; dr; dr = dr->next) {
			buf_u32(&b, (unsigned int)i);
			buf_u32(&b, (unsigned int)dr->offset);
			buf_u32(&b,
				(unsigned int)sym_index(arr, nsyms, dr->sym));
			buf_u64(&b, (unsigned long)dr->addend);
		}
	}
	for (r = u->relocs; r; r = r->next) {
		buf_u32(&b, (unsigned int)r->pos);
		buf_u32(&b, (unsigned int)r->kind);
		buf_u32(&b, (unsigned int)sym_index(arr, nsyms, r->sym));
		buf_u64(&b, (unsigned long)r->addend);
	}

	write_file_bytes(path, b.data, b.len, 0644);
	free(arr);
}

struct unit *read_object(const char *path)
{
	struct unit *u;
	struct sym **arr;
	struct sym *tail = NULL;
	char *buf;
	int len;
	struct rd rd;
	unsigned int nsyms;
	unsigned int ndrels;
	unsigned int nrelocs;
	int i;

	if (try_read_file(path, &buf, &len) < 0)
		fatal("cannot open object file '%s'", path);
	memset(&rd, 0, sizeof(rd));
	rd.p = (const unsigned char *)buf;
	rd.len = len;
	if (rd_u8(&rd) != 'C' || rd_u8(&rd) != 'C' || rd_u8(&rd) != 'O' ||
	    rd_u8(&rd) != '1')
		fatal("'%s' is not a cc object file", path);
	if (rd_u32(&rd) != 1)
		fatal("unsupported object file version in '%s'", path);

	u = xcalloc(1, sizeof(*u));
	u->code_len = (int)rd_u32(&rd);
	u->data_len = (int)rd_u32(&rd);
	u->bss_len = (int)rd_u32(&rd);
	nsyms = rd_u32(&rd);
	ndrels = rd_u32(&rd);
	nrelocs = rd_u32(&rd);

	u->code = xmalloc((size_t)(u->code_len > 0 ? u->code_len : 1));
	rd_bytes(&rd, u->code, u->code_len);
	u->data = xmalloc((size_t)(u->data_len > 0 ? u->data_len : 1));
	rd_bytes(&rd, u->data, u->data_len);

	arr = xcalloc((size_t)(nsyms > 0 ? nsyms : 1), sizeof(*arr));
	for (i = 0; i < (int)nsyms; i++) {
		struct sym *s = xcalloc(1, sizeof(*s));

		s->name = rd_name(&rd);
		s->sec = (int)rd_u32(&rd);
		s->offset = (int)rd_u32(&rd);
		s->size = (int)rd_u32(&rd);
		s->align = (int)rd_u32(&rd);
		s->defined = rd_u8(&rd) != 0;
		s->is_static = rd_u8(&rd) != 0;
		arr[i] = s;
		if (tail)
			tail->next = s;
		else
			u->syms = s;
		tail = s;
	}

	for (i = 0; i < (int)ndrels; i++) {
		unsigned int owner = rd_u32(&rd);
		unsigned int off = rd_u32(&rd);
		unsigned int target = rd_u32(&rd);
		unsigned long addend = rd_u64(&rd);
		struct data_reloc *dr;

		if (owner >= nsyms || target >= nsyms)
			fatal("bad symbol index in object file '%s'", path);
		dr = xcalloc(1, sizeof(*dr));
		dr->offset = (int)off;
		dr->sym = arr[target];
		dr->addend = (long)addend;
		dr->next = arr[owner]->drel;
		arr[owner]->drel = dr;
	}
	for (i = 0; i < (int)nrelocs; i++) {
		unsigned int pos = rd_u32(&rd);
		unsigned int kind = rd_u32(&rd);
		unsigned int target = rd_u32(&rd);
		unsigned long addend = rd_u64(&rd);
		struct reloc *r;

		if (target >= nsyms)
			fatal("bad symbol index in object file '%s'", path);
		r = xcalloc(1, sizeof(*r));
		r->pos = (int)pos;
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
