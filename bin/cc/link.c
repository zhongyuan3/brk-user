#include "brkcc.h"

#include <limits.h>

/* ------------------------------------------------------------------ */
/* ELF writer                                                          */
/* ------------------------------------------------------------------ */

#define USER_BASE 0x10000UL
#define TEXT_FILE_OFF 0x1000
#define EM_RISCV 243
#define EF_RISCV_FLOAT_ABI_DOUBLE 4
#define LINK_MAX_ALIGN 4096
#define LINK_MAX_SECTION 0x40000000

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
	if (s->is_static)
		fatal("undefined static symbol '%s'", s->name);
	l = link_find(s->name);
	if (!l)
		fatal("undefined symbol '%s'", s->name);
	return l->sym->addr;
}

static unsigned long add_addend(unsigned long addr, long addend,
				const char *what, const char *name)
{
	unsigned long mag;

	if (addend < 0) {
		mag = (unsigned long)(-(addend + 1)) + 1;
		if (mag > addr)
			fatal("%s relocation underflow for '%s'", what, name);
		return addr - mag;
	}
	mag = (unsigned long)addend;
	if (mag > (unsigned long)-1 - addr)
		fatal("%s relocation overflow for '%s'", what, name);
	return addr + mag;
}

static void put_ins(unsigned char *buf, int pos, unsigned int ins)
{
	buf[pos + 0] = (unsigned char)(ins & 0xff);
	buf[pos + 1] = (unsigned char)((ins >> 8) & 0xff);
	buf[pos + 2] = (unsigned char)((ins >> 16) & 0xff);
	buf[pos + 3] = (unsigned char)((ins >> 24) & 0xff);
}

static int sym_alignment(struct sym *s)
{
	if (s->align <= 0 || s->align > LINK_MAX_ALIGN)
		fatal("internal error: bad alignment for symbol '%s'", s->name);
	if (s->align & (s->align - 1))
		fatal("internal error: non-power-of-two alignment for symbol '%s'",
		      s->name);
	return s->align;
}

static int align_off(int off, int a, const char *what)
{
	int pad;

	if (off < 0)
		fatal("internal error: negative %s offset", what);
	pad = (a - off % a) % a;
	if (pad > INT_MAX - off)
		fatal("internal error: %s section too large", what);
	return off + pad;
}

void finalize_unit(void)
{
	/*
	 * The back end records instructions in a structured list and keeps the
	 * byte buffer synchronized for compatibility.  Encode the final records
	 * here, after all fixups have been applied, so the unit always carries
	 * the authoritative text bytes.
	 */
	asm_finalize();
	struct unit *u = xcalloc(1, sizeof(*u));
	struct sym *s;
	struct buf data;
	int off;

	if (code.len < 0 || code.len > LINK_MAX_SECTION)
		fatal("text section too large");
	u->code_len = code.len;
	u->code = xmalloc((size_t)(code.len > 0 ? code.len : 1));
	if (code.len > 0)
		memcpy(u->code, code.data, (size_t)code.len);
	u->syms = syms;
	u->relocs = relocs;

	for (s = syms; s; s = s->next) {
		if (s->sec < SEC_TEXT || s->sec > SEC_BSS)
			fatal("internal error: bad section for symbol '%s'",
			      s->name);
		if (!s->defined)
			continue;
		if (s->size < 0 || s->offset < 0)
			fatal("internal error: bad extent for symbol '%s'",
			      s->name);
		if (s->sec == SEC_TEXT &&
		    (s->offset > u->code_len || (s->offset % 4)))
			fatal("internal error: text symbol '%s' outside text section",
			      s->name);
	}

	off = 0;
	for (s = syms; s; s = s->next) {
		if (!s->defined || s->sec != SEC_DATA)
			continue;
		off = align_off(off, sym_alignment(s), "data");
		s->offset = off;
		if (s->size > LINK_MAX_SECTION - off)
			fatal("data section too large");
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
		off = align_off(off, sym_alignment(s), "bss");
		s->offset = off;
		if (s->size > LINK_MAX_SECTION - off)
			fatal("bss section too large");
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
		unsigned long target;
		unsigned long pos_abs;
		unsigned int old;
		unsigned int ins;

		if (r->pos < 0 || r->pos > u->code_len - 4 || (r->pos % 4))
			fatal("text relocation out of range in '%s'",
			      r->sym->name);
		target = add_addend(resolve_sym(r->sym), r->addend, "text",
				    r->sym->name);
		if (target > 0x7fffffffUL)
			fatal("relocation target out of range for '%s'",
			      r->sym->name);
		pos_abs = text_section_base + u->text_base +
			  (unsigned long)r->pos;

		memcpy(&old, u->code + r->pos, 4);
		switch (r->kind) {
		case R_HI20: {
			long hi = (long)((target + 0x800) >> 12);

			if (hi > (1L << 19) - 1)
				fatal("relocation target out of range for '%s'",
				      r->sym->name);
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
			if (off < -(1L << 20) || off >= (1L << 20))
				fatal("program too large for direct call");
			ins = enc_j(0x6f, (int)rd_of(old), (int)off);
			break;
		}
		default:
			fatal("bad relocation kind %d in object", r->kind);
			return;
		}
		put_ins(u->code, r->pos, ins);
	}

	for (s = u->syms; s; s = s->next) {
		struct data_reloc *dr;

		if (!s->defined || s->sec != SEC_DATA)
			continue;
		for (dr = s->drel; dr; dr = dr->next) {
			unsigned long target;
			unsigned long at;

			if (dr->offset < 0 || s->size < 8 ||
			    dr->offset > s->size - 8)
				fatal("data relocation out of range for '%s'",
				      s->name);
			target = add_addend(resolve_sym(dr->sym), dr->addend,
					    "data", s->name);
			at = (unsigned long)s->offset +
			     (unsigned long)dr->offset;
			if (at > (unsigned long)u->data_len ||
			    (unsigned long)u->data_len - at < 8)
				fatal("data relocation out of range for '%s'",
				      s->name);
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

static unsigned long align_up(unsigned long n, unsigned long a,
			      const char *what)
{
	unsigned long pad;

	if (a == 0 || a > LINK_MAX_ALIGN || (a & (a - 1)))
		fatal("internal error: bad %s alignment", what);
	pad = (a - n % a) % a;
	if (n > (unsigned long)-1 - pad)
		fatal("program too large");
	return n + pad;
}

static unsigned long unit_align(struct unit *u, int sec)
{
	unsigned long a = 16;
	struct sym *s;

	for (s = u->syms; s; s = s->next) {
		unsigned long sa;

		if (!s->defined || s->sec != sec)
			continue;
		sa = (unsigned long)sym_alignment(s);
		if (sa > a)
			a = sa;
	}
	return a;
}

static unsigned long section_align(int sec)
{
	unsigned long a = 16;
	struct unit *u;

	for (u = units; u; u = u->next) {
		unsigned long ua = unit_align(u, sec);

		if (ua > a)
			a = ua;
	}
	return a;
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
		text_off = align_up(text_off, unit_align(u, SEC_TEXT), "text");
		if (text_off > LINK_MAX_SECTION)
			fatal("program too large");
		u->text_base = text_off;
		if ((unsigned long)u->code_len >
		    (unsigned long)LINK_MAX_SECTION - text_off)
			fatal("program too large");
		text_off += (unsigned long)u->code_len;
	}
	for (u = units; u; u = u->next) {
		data_off = align_up(data_off, unit_align(u, SEC_DATA), "data");
		if (data_off > LINK_MAX_SECTION)
			fatal("program too large");
		u->data_base = data_off;
		if ((unsigned long)u->data_len >
		    (unsigned long)LINK_MAX_SECTION - data_off)
			fatal("program too large");
		data_off += (unsigned long)u->data_len;
	}
	for (u = units; u; u = u->next) {
		bss_off = align_up(bss_off, unit_align(u, SEC_BSS), "bss");
		if (bss_off > LINK_MAX_SECTION)
			fatal("program too large");
		u->bss_base = bss_off;
		if ((unsigned long)u->bss_len >
		    (unsigned long)LINK_MAX_SECTION - bss_off)
			fatal("program too large");
		bss_off += (unsigned long)u->bss_len;
	}

	text_section_base = USER_BASE + TEXT_FILE_OFF;
	data_section_base = align_up(text_section_base + text_off,
				     section_align(SEC_DATA), "data section");
	bss_section_base = align_up(data_section_base + data_off,
				    section_align(SEC_BSS), "bss section");
	if (data_section_base - USER_BASE > (unsigned long)INT_MAX)
		fatal("program too large");

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
	if (file_len > (unsigned long)INT_MAX ||
	    mem_len > (unsigned long)INT_MAX)
		fatal("program too large");
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
	put32(file + 48, EF_RISCV_FLOAT_ABI_DOUBLE); /* e_flags: hard-FP */
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
