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

static unsigned int get_u32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
	       ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned int get_u16(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned long get_bytes(const unsigned char *p, int size)
{
	unsigned long v = 0;
	int i;

	for (i = 0; i < size; i++)
		v |= (unsigned long)p[i] << (i * 8);
	return v;
}

static void put_bytes(unsigned char *p, unsigned long v, int size)
{
	int i;

	for (i = 0; i < size; i++)
		p[i] = (unsigned char)((v >> (i * 8)) & 0xff);
}

static void put_half(unsigned char *buf, int pos, unsigned int ins)
{
	buf[pos + 0] = (unsigned char)(ins & 0xff);
	buf[pos + 1] = (unsigned char)((ins >> 8) & 0xff);
}

static unsigned long hi20_value(long long diff, const char *name)
{
	long long hi = (diff + 0x800) >> 12;

	if (hi < -(1LL << 19) || hi > (1LL << 19) - 1)
		fatal("relocation target out of range for '%s'", name);
	return (unsigned long)hi & 0xfffffUL;
}

static long lo12_value(long long diff)
{
	long lo = (long)(diff & 0xfff);

	if (lo & 0x800)
		lo -= 0x1000;
	return lo;
}

static unsigned int rvc_cb(unsigned int old, long off, const char *name)
{
	unsigned int imm;

	if ((old & 0x3) != 1 || ((old >> 13) & 7) < 6 ||
	    ((old >> 13) & 7) > 7)
		fatal("invalid RVC branch instruction for '%s'", name);
	if (off < -256 || off > 254 || (off & 1))
		fatal("RVC branch out of range for '%s'", name);
	imm = (unsigned int)off;
	old &= ~(0x1c00u | 0x007cu);
	old |= (((imm >> 8) & 1) << 12);
	old |= (((imm >> 3) & 3) << 10);
	old |= (((imm >> 6) & 3) << 5);
	old |= (((imm >> 1) & 3) << 3);
	old |= (((imm >> 5) & 1) << 2);
	return old;
}

static unsigned int rvc_cj(unsigned int old, long off, const char *name)
{
	unsigned int imm;

	if ((old & 0x3) != 1 || ((old >> 13) & 7) != 5)
		fatal("invalid RVC jump instruction for '%s'", name);
	if (off < -2048 || off > 2046 || (off & 1))
		fatal("RVC jump out of range for '%s'", name);
	imm = (unsigned int)off;
	old &= ~0x1ffcu;
	old |= (((imm >> 11) & 1) << 12);
	old |= (((imm >> 4) & 1) << 11);
	old |= (((imm >> 8) & 3) << 9);
	old |= (((imm >> 10) & 1) << 8);
	old |= (((imm >> 6) & 1) << 7);
	old |= (((imm >> 7) & 1) << 6);
	old |= (((imm >> 1) & 7) << 3);
	old |= (((imm >> 5) & 1) << 2);
	return old;
}

static struct reloc *find_pcrel_hi(struct unit *u, struct reloc *lo)
{
	struct reloc *r;

	for (r = u->relocs; r; r = r->next) {
		if (r->kind == R_PCREL_HI20 &&
		    r->pos == lo->sym->offset)
			return r;
	}
	return NULL;
}

static void patch_one_text_reloc(struct unit *u, struct reloc *r,
				 unsigned long text_section_base)
{
	unsigned long target;
	unsigned long pos_abs;
	unsigned long data;
	unsigned int old;
	long long diff;
	long lo;

	pos_abs = text_section_base + u->text_base +
		  (unsigned long)r->pos;

	if (r->kind == R_RVC_BRANCH || r->kind == R_RVC_JUMP) {
		target = add_addend(resolve_sym(r->sym), r->addend, "text",
				    r->sym->name);
		diff = (long long)target - (long long)pos_abs;
		if (r->kind == R_RVC_BRANCH)
			put_half(u->code, r->pos,
				 rvc_cb(get_u16(u->code + r->pos),
					(long)diff, r->sym->name));
		else
			put_half(u->code, r->pos,
				 rvc_cj(get_u16(u->code + r->pos),
					(long)diff, r->sym->name));
		return;
	}

	old = get_u32(u->code + r->pos);

	switch (r->kind) {
	case R_HI20:
		target = add_addend(resolve_sym(r->sym), r->addend, "text",
				    r->sym->name);
		data = hi20_value((long long)target, r->sym->name);
		put_ins(u->code, r->pos,
			(old & 0xfff) | ((unsigned int)data << 12));
		return;
	case R_PCREL_HI20:
		target = add_addend(resolve_sym(r->sym), r->addend, "text",
				    r->sym->name);
		diff = (long long)target - (long long)pos_abs;
		data = hi20_value(diff, r->sym->name);
		put_ins(u->code, r->pos,
			(old & 0xfff) | ((unsigned int)data << 12));
		return;
	case R_LO12I:
		target = add_addend(resolve_sym(r->sym), r->addend, "text",
				    r->sym->name);
		lo = lo12_value((long long)target);
		put_ins(u->code, r->pos,
			(old & 0x000fffffUL) |
				(((unsigned long)lo & 0xfff) << 20));
		return;
	case R_LO12S:
		target = add_addend(resolve_sym(r->sym), r->addend, "text",
				    r->sym->name);
		lo = lo12_value((long long)target);
		old &= ~((0x7fu << 25) | (0x1fu << 7));
		old |= (((unsigned long)lo >> 5) & 0x7f) << 25;
		old |= ((unsigned long)lo & 0x1f) << 7;
		put_ins(u->code, r->pos, old);
		return;
	case R_PCREL_LO12I: {
		struct reloc *hi = find_pcrel_hi(u, r);

		if (!hi)
			fatal("missing PCREL_HI20 pair for '%s'",
			      r->sym->name);
		target = add_addend(resolve_sym(hi->sym), hi->addend, "text",
				    hi->sym->name);
		{
			unsigned long hi_pos = text_section_base +
					       u->text_base +
					       (unsigned long)hi->pos;
			long long value = (long long)target -
					  (long long)hi_pos;

			lo = lo12_value(value);
		}
		put_ins(u->code, r->pos,
			(old & 0x000fffffUL) |
				(((unsigned long)lo & 0xfff) << 20));
		return;
	}
	case R_PCREL_LO12S: {
		struct reloc *hi = find_pcrel_hi(u, r);

		if (!hi)
			fatal("missing PCREL_HI20 pair for '%s'",
			      r->sym->name);
		target = add_addend(resolve_sym(hi->sym), hi->addend, "text",
				    hi->sym->name);
		{
			unsigned long hi_pos = text_section_base +
					       u->text_base +
					       (unsigned long)hi->pos;
			long long value = (long long)target -
					  (long long)hi_pos;

			lo = lo12_value(value);
		}
		old &= ~((0x7fu << 25) | (0x1fu << 7));
		old |= (((unsigned long)lo >> 5) & 0x7f) << 25;
		old |= ((unsigned long)lo & 0x1f) << 7;
		put_ins(u->code, r->pos, old);
		return;
	}
	case R_JAL:
		target = add_addend(resolve_sym(r->sym), r->addend, "text",
				    r->sym->name);
		diff = (long long)target - (long long)pos_abs;
		if ((diff & 1) || diff < -(1LL << 20) || diff >= (1LL << 20))
			fatal("program too large for direct call to '%s'",
			      r->sym->name);
		put_ins(u->code, r->pos,
			enc_j(0x6f, (int)((old >> 7) & 0x1f), (int)diff));
		return;
	case R_BRANCH:
		target = add_addend(resolve_sym(r->sym), r->addend, "text",
				    r->sym->name);
		diff = (long long)target - (long long)pos_abs;
		if ((diff & 1) || diff < -(1LL << 12) || diff >= (1LL << 12))
			fatal("branch out of range for '%s'", r->sym->name);
		put_ins(u->code, r->pos,
			enc_b(0x63, (int)((old >> 12) & 7),
			      (int)((old >> 15) & 0x1f),
			      (int)((old >> 20) & 0x1f), (int)diff));
		return;
	case R_CALL:
		target = add_addend(resolve_sym(r->sym), r->addend, "text",
				    r->sym->name);
		if (r->pos > u->code_len - 8)
			fatal("truncated call relocation in '%s'", r->sym->name);
		diff = (long long)target - (long long)pos_abs;
		{
			unsigned int old2 = get_u32(u->code + r->pos + 4);
			unsigned long hi = hi20_value(diff, r->sym->name);
			unsigned long v = hi << 12;
			long lo2 = lo12_value(diff - (long long)v);

			if ((old & 0x7f) != 0x17 ||
			    (old2 & 0x7f) != 0x67)
				fatal("invalid call relocation pair for '%s'",
				      r->sym->name);
			put_ins(u->code, r->pos,
				(old & 0xfff) | ((unsigned int)hi << 12));
			put_ins(u->code, r->pos + 4,
				(old2 & 0x000fffffUL) |
					(((unsigned long)lo2 & 0xfff) << 20));
		}
		return;
	default:
		fatal("bad relocation kind %d in object", r->kind);
	}
}

static void patch_unit_relocs(struct unit *u, unsigned long text_section_base,
			      unsigned long data_section_base)
{
	struct reloc *r;
	struct sym *s;

	for (r = u->relocs; r; r = r->next) {
		int need = 4;

		if (r->kind == R_RVC_BRANCH || r->kind == R_RVC_JUMP)
			need = 2;
		else if (r->kind == R_CALL)
			need = 8;
		if (r->pos < 0 || r->pos > u->code_len - need)
			fatal("text relocation out of range in '%s'",
			      r->sym->name);
		patch_one_text_reloc(u, r, text_section_base);
	}

	for (s = u->syms; s; s = s->next) {
		struct data_reloc *dr;

		if (!s->defined || s->sec != SEC_DATA)
			continue;
		for (dr = s->drel; dr; dr = dr->next) {
			unsigned long target;
			unsigned long at;
			unsigned long place;
			unsigned long value;

			if (dr->offset < 0 || dr->size <= 0 ||
			    (dr->size != 1 && dr->size != 2 &&
			     dr->size != 4 && dr->size != 8) ||
			    s->size < dr->size ||
			    dr->offset > s->size - dr->size)
				fatal("data relocation out of range for '%s'",
				      s->name);
			target = add_addend(resolve_sym(dr->sym), dr->addend,
					    "data", s->name);
			at = (unsigned long)s->offset +
			     (unsigned long)dr->offset;
			if (at > (unsigned long)u->data_len ||
			    (unsigned long)u->data_len - at <
				    (unsigned long)dr->size)
				fatal("data relocation out of range for '%s'",
				      s->name);
			place = data_section_base + u->data_base + at;
			switch (dr->kind) {
			case DR_ABS:
				if (dr->size == 4 &&
				    target > 0xffffffffUL)
					fatal("32-bit data relocation overflow for '%s'",
					      s->name);
				put_bytes(u->data + at, target, dr->size);
				break;
			case DR_PCREL:
				value = (unsigned long)
					((long long)target - (long long)place);
				put_bytes(u->data + at, value, dr->size);
				break;
			case DR_ADD:
				value = get_bytes(u->data + at, dr->size) +
					target;
				put_bytes(u->data + at, value, dr->size);
				break;
			case DR_SUB:
				value = get_bytes(u->data + at, dr->size) -
					target;
				put_bytes(u->data + at, value, dr->size);
				break;
			default:
				fatal("bad data relocation kind for '%s'",
				      s->name);
			}
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
	unsigned long data_file_off;
	unsigned long file_len;
	unsigned long mem_len;
	unsigned long entry;
	unsigned int out_flags = EF_RISCV_FLOAT_ABI_DOUBLE;
	unsigned char *text;
	unsigned char *data;
	unsigned char *file;
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

	/*
	 * Put writable data in its own page-aligned segment so the text
	 * segment can stay read+execute only.
	 */
	data_file_off = align_up(TEXT_FILE_OFF + text_off, 0x1000,
				 "data file offset");
	if (data_file_off > (unsigned long)INT_MAX)
		fatal("program too large");
	data_section_base = align_up(USER_BASE + data_file_off,
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
		patch_unit_relocs(u, text_section_base, data_section_base);

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

	file_len = data_file_off + data_off;
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
	for (u = units; u; u = u->next) {
		if (u->uses_rvc) {
			out_flags |= 1; /* EF_RISCV_RVC */
			break;
		}
	}
	put32(file + 48, out_flags);
	put16(file + 52, 64); /* e_ehsize */
	put16(file + 54, 56); /* e_phentsize */
	put16(file + 56, 2); /* e_phnum */
	put16(file + 58, 0); /* e_shentsize */
	put16(file + 60, 0); /* e_shnum */
	put16(file + 62, 0); /* e_shstrndx */

	/* program header 0: headers + text, read+execute */
	put32(file + 64, 1); /* PT_LOAD */
	put32(file + 68, 5); /* R|X */
	put64(file + 72, 0); /* p_offset */
	put64(file + 80, USER_BASE);
	put64(file + 88, USER_BASE);
	put64(file + 96, data_file_off);
	put64(file + 104, data_file_off);
	put64(file + 112, 0x1000);

	/* program header 1: data + bss, read+write, bss outside the file */
	put32(file + 120, 1); /* PT_LOAD */
	put32(file + 124, 6); /* R|W */
	put64(file + 128, data_file_off);
	put64(file + 136, USER_BASE + data_file_off);
	put64(file + 144, USER_BASE + data_file_off);
	put64(file + 152, data_off);
	put64(file + 160, mem_len - data_file_off);
	put64(file + 168, 0x1000);

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
