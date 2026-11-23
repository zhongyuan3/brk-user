#include "brkcc.h"

#include <limits.h>

/*
 * Standard ELF64 little-endian RISC-V relocatable object support.
 *
 * `-c` writes these objects.  The CCO2/CCO1 formats remain readable (and can
 * still be written with --cco2-object for compatibility tests), but ELF is
 * the canonical external representation from this point on.
 */

#define EI_NIDENT 16

#define ET_REL 1
#define EM_RISCV 243
#define EF_RISCV_FLOAT_ABI_DOUBLE 4

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHT_RISCV_ATTRIBUTES 0x70000003

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define SHF_INFO_LINK 0x40

#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4

#define R_RISCV_32 1
#define R_RISCV_64 2
#define R_RISCV_32_PCREL 7
#define R_RISCV_BRANCH 16
#define R_RISCV_JAL 17
#define R_RISCV_CALL 18
#define R_RISCV_CALL_PLT 19
#define R_RISCV_PCREL_HI20 23
#define R_RISCV_PCREL_LO12_I 24
#define R_RISCV_PCREL_LO12_S 25
#define R_RISCV_HI20 26
#define R_RISCV_LO12_I 27
#define R_RISCV_LO12_S 28
#define R_RISCV_ADD8 33
#define R_RISCV_ADD16 34
#define R_RISCV_ADD32 35
#define R_RISCV_ADD64 36
#define R_RISCV_SUB8 37
#define R_RISCV_SUB16 38
#define R_RISCV_SUB32 39
#define R_RISCV_SUB64 40
#define R_RISCV_ALIGN 43
#define R_RISCV_RVC_BRANCH 44
#define R_RISCV_RVC_JUMP 45
#define R_RISCV_RELAX 51

#define ELF_MAX_BYTES 0x40000000

enum {
	SH_NULL = 0,
	SH_TEXT,
	SH_DATA,
	SH_BSS,
	SH_RELA_TEXT,
	SH_RELA_DATA,
	SH_SYMTAB,
	SH_STRTAB,
	SH_ATTR,
	SH_SHSTRTAB,
	SH_COUNT,
};

static void elf_put16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void elf_put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void elf_put64(unsigned char *p, unsigned long v)
{
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (unsigned char)((v >> (i * 8)) & 0xff);
}

static unsigned int elf_str_add(struct buf *b, const char *s)
{
	size_t n;
	unsigned int off;

	if (!s)
		fatal("internal error: ELF symbol without a name");
	n = strlen(s);
	off = (unsigned int)b->len;
	if ((unsigned long)b->len + n + 1 > 0xffffffffUL)
		fatal("ELF string table too large");
	buf_grow(b, (int)n + 1);
	memcpy(b->data + b->len, s, n);
	b->len += (int)n;
	buf_byte(b, 0);
	return off;
}

static int elf_sym_alignment(struct sym *s)
{
	if (s->align <= 0 || s->align > 4096)
		fatal("internal error: bad alignment for symbol '%s'", s->name);
	if (s->align & (s->align - 1))
		fatal("internal error: non-power-of-two alignment for symbol '%s'",
		      s->name);
	return s->align;
}

static unsigned long elf_section_align(struct unit *u, int sec)
{
	unsigned long a = 1;
	struct sym *s;

	for (s = u->syms; s; s = s->next) {
		unsigned long sa;

		if (!s->defined || s->sec != sec)
			continue;
		sa = (unsigned long)elf_sym_alignment(s);
		if (sa > a)
			a = sa;
	}
	return a;
}

static int elf_sym_section(struct sym *s)
{
	switch (s->sec) {
	case SEC_TEXT:
		return SH_TEXT;
	case SEC_DATA:
		return SH_DATA;
	case SEC_BSS:
		return SH_BSS;
	default:
		fatal("internal error: bad section for symbol '%s'", s->name);
		return 0;
	}
}

static void elf_append(struct buf *b, const void *p, int n)
{
	if (n < 0 || b->len < 0 || b->len > ELF_MAX_BYTES ||
	    n > ELF_MAX_BYTES - b->len)
		fatal("ELF object too large");
	buf_grow(b, n);
	if (n)
		memcpy(b->data + b->len, p, (size_t)n);
	b->len += n;
}

static void elf_rela(struct buf *b, unsigned long off, unsigned int type,
		     unsigned int symidx, long addend)
{
	buf_u64(b, off);
	buf_u64(b, ((unsigned long)symidx << 32) | (unsigned long)type);
	buf_u64(b, (unsigned long)addend);
	if (b->len > ELF_MAX_BYTES)
		fatal("ELF object too large");
}

static void write_elf_bytes(const char *path, const unsigned char *buf,
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

void write_elf_object(const char *path, struct unit *u)
{
	struct buf out;
	struct buf rela_text;
	struct buf rela_data;
	struct buf symtab;
	struct buf strtab;
	struct buf attr;
	struct buf shstrtab;
	struct sym **arr;
	int *symidx;
	struct sym *s;
	struct reloc *r;
	unsigned long shoff[SH_COUNT] = { 0 };
	unsigned long shsize[SH_COUNT] = { 0 };
	unsigned long shalign[SH_COUNT] = { 0 };
	unsigned int shname[SH_COUNT] = { 0 };
	unsigned long text_align;
	unsigned long data_align;
	unsigned long bss_align;
	unsigned long e_shoff;
	int nsyms = 0;
	int nlocal = 0;
	int first_global;
	int i;

	if (!u || u->code_len < 0 || u->data_len < 0 || u->bss_len < 0)
		fatal("internal error: invalid unit");
	if ((unsigned long)u->code_len > ELF_MAX_BYTES ||
	    (unsigned long)u->data_len > ELF_MAX_BYTES)
		fatal("ELF section too large");

	for (s = u->syms; s; s = s->next) {
		if (nsyms == INT_MAX)
			fatal("too many symbols in object");
		if (s->is_static)
			nlocal++;
		nsyms++;
	}
	first_global = 1 + 3 + nlocal;

	arr = xcalloc((size_t)(nsyms > 0 ? nsyms : 1), sizeof(*arr));
	symidx = xcalloc((size_t)(nsyms > 0 ? nsyms : 1), sizeof(*symidx));
	i = 0;
	for (s = u->syms; s; s = s->next)
		arr[i++] = s;

	{
		int li = 1 + 3;
		int gi = first_global;

		for (i = 0; i < nsyms; i++)
			symidx[i] = arr[i]->is_static ? li++ : gi++;
	}

	buf_init(&strtab);
	buf_byte(&strtab, 0);
	buf_init(&symtab);
	buf_pad(&symtab, 24); /* STN_UNDEF */

	/* Section symbols. */
	for (i = 0; i < 3; i++) {
		unsigned int shndx = (unsigned int)(SH_TEXT + i);

		buf_u32(&symtab, 0);
		buf_byte(&symtab, (STB_LOCAL << 4) | STT_SECTION);
		buf_byte(&symtab, 0);
		buf_byte(&symtab, (int)(shndx & 0xff));
		buf_byte(&symtab, (int)((shndx >> 8) & 0xff));
		buf_u64(&symtab, 0);
		buf_u64(&symtab, 0);
	}

	for (i = 0; i < nsyms; i++) {
		if (!arr[i]->is_static)
			continue;
		s = arr[i];
		buf_u32(&symtab, elf_str_add(&strtab, s->name));
		buf_byte(&symtab, (STB_LOCAL << 4) |
				   (s->sec == SEC_TEXT ? STT_FUNC : STT_OBJECT));
		buf_byte(&symtab, 0);
		if (s->defined) {
			unsigned int shndx = (unsigned int)elf_sym_section(s);

			buf_byte(&symtab, (int)(shndx & 0xff));
			buf_byte(&symtab, (int)((shndx >> 8) & 0xff));
			buf_u64(&symtab, (unsigned long)s->offset);
			buf_u64(&symtab, (unsigned long)s->size);
		} else {
			buf_byte(&symtab, 0);
			buf_byte(&symtab, 0);
			buf_u64(&symtab, 0);
			buf_u64(&symtab, 0);
		}
	}
	for (i = 0; i < nsyms; i++) {
		if (arr[i]->is_static)
			continue;
		s = arr[i];
		buf_u32(&symtab, elf_str_add(&strtab, s->name));
		buf_byte(&symtab, (STB_GLOBAL << 4) |
				   (s->sec == SEC_TEXT ? STT_FUNC : STT_OBJECT));
		buf_byte(&symtab, 0);
		if (s->defined) {
			unsigned int shndx = (unsigned int)elf_sym_section(s);

			buf_byte(&symtab, (int)(shndx & 0xff));
			buf_byte(&symtab, (int)((shndx >> 8) & 0xff));
			buf_u64(&symtab, (unsigned long)s->offset);
			buf_u64(&symtab, (unsigned long)s->size);
		} else {
			buf_byte(&symtab, 0);
			buf_byte(&symtab, 0);
			buf_u64(&symtab, 0);
			buf_u64(&symtab, 0);
		}
	}

	buf_init(&rela_text);
	for (r = u->relocs; r; r = r->next) {
		unsigned int type;
		int idx = -1;

		switch (r->kind) {
		case R_HI20:
			type = R_RISCV_HI20;
			break;
		case R_LO12I:
			type = R_RISCV_LO12_I;
			break;
		case R_JAL:
			type = R_RISCV_JAL;
			break;
		default:
			fatal("internal error: bad relocation kind");
		}
		for (i = 0; i < nsyms; i++) {
			if (arr[i] == r->sym) {
				idx = symidx[i];
				break;
			}
		}
		if (idx < 0)
			fatal("internal error: relocation target is not in the symbol table");
		elf_rela(&rela_text, (unsigned long)r->pos, type,
			 (unsigned int)idx, r->addend);
	}

	buf_init(&rela_data);
	for (i = 0; i < nsyms; i++) {
		struct data_reloc *dr;

		s = arr[i];
		if (!s->defined || s->sec != SEC_DATA)
			continue;
		for (dr = s->drel; dr; dr = dr->next) {
			unsigned long at = (unsigned long)s->offset +
					   (unsigned long)dr->offset;
			int idx = -1;
			int j;

			if (dr->offset < 0 || dr->kind != DR_ABS ||
			    (dr->size != 4 && dr->size != 8) ||
			    s->size < dr->size ||
			    dr->offset > s->size - dr->size)
				fatal("data relocation out of range for '%s'",
				      s->name);
			for (j = 0; j < nsyms; j++) {
				if (arr[j] == dr->sym) {
					idx = symidx[j];
					break;
				}
			}
			if (idx < 0)
				fatal("internal error: data relocation target is not in the symbol table");
			elf_rela(&rela_data, at,
				 dr->size == 4 ? R_RISCV_32 : R_RISCV_64,
				 (unsigned int)idx, dr->addend);
		}
	}

	text_align = 4;
	data_align = elf_section_align(u, SEC_DATA);
	bss_align = elf_section_align(u, SEC_BSS);

	{
		static const char arch[] = "rv64i2p1_m2p0_f2p2_d2p2";
		/* stack-align tag+value, arch tag, arch string including NUL */
		unsigned int attrs_len = 3 + (unsigned int)sizeof(arch);
		unsigned int sub_len = 4 + 1 + attrs_len;
		unsigned int vendor_len = 4 + 6 + 1 + 4 + attrs_len;

		buf_init(&attr);
		buf_byte(&attr, 'A');
		buf_u32(&attr, vendor_len);
		elf_append(&attr, "riscv", 6);
		buf_byte(&attr, 1); /* Tag_File */
		buf_u32(&attr, sub_len);
		buf_byte(&attr, 4);  /* Tag_RISCV_stack_align */
		buf_byte(&attr, 16);
		buf_byte(&attr, 5);  /* Tag_RISCV_arch */
		elf_append(&attr, arch, (int)sizeof(arch));
	}

	buf_init(&shstrtab);
	buf_byte(&shstrtab, 0);
	{
		static const char *const names[SH_COUNT] = {
			"", ".text", ".data", ".bss", ".rela.text",
			".rela.data", ".symtab", ".strtab",
			".riscv.attributes", ".shstrtab",
		};

		for (i = 1; i < SH_COUNT; i++)
			shname[i] = elf_str_add(&shstrtab, names[i]);
	}
	shname[SH_NULL] = 0;

	/* Lay out the file. */
	buf_init(&out);
	buf_pad(&out, 64);
	buf_align(&out, (int)text_align);
	shoff[SH_TEXT] = (unsigned long)out.len;
	shsize[SH_TEXT] = (unsigned long)u->code_len;
	shalign[SH_TEXT] = text_align;
	elf_append(&out, u->code, u->code_len);

	buf_align(&out, (int)data_align);
	shoff[SH_DATA] = (unsigned long)out.len;
	shsize[SH_DATA] = (unsigned long)u->data_len;
	shalign[SH_DATA] = data_align;
	elf_append(&out, u->data, u->data_len);

	buf_align(&out, (int)bss_align);
	shoff[SH_BSS] = (unsigned long)out.len;
	shsize[SH_BSS] = (unsigned long)u->bss_len;
	shalign[SH_BSS] = bss_align;

	buf_align(&out, 8);
	shoff[SH_RELA_TEXT] = (unsigned long)out.len;
	shsize[SH_RELA_TEXT] = (unsigned long)rela_text.len;
	shalign[SH_RELA_TEXT] = 8;
	elf_append(&out, rela_text.data, rela_text.len);

	buf_align(&out, 8);
	shoff[SH_RELA_DATA] = (unsigned long)out.len;
	shsize[SH_RELA_DATA] = (unsigned long)rela_data.len;
	shalign[SH_RELA_DATA] = 8;
	elf_append(&out, rela_data.data, rela_data.len);

	buf_align(&out, 8);
	shoff[SH_SYMTAB] = (unsigned long)out.len;
	shsize[SH_SYMTAB] = (unsigned long)symtab.len;
	shalign[SH_SYMTAB] = 8;
	elf_append(&out, symtab.data, symtab.len);

	shoff[SH_STRTAB] = (unsigned long)out.len;
	shsize[SH_STRTAB] = (unsigned long)strtab.len;
	shalign[SH_STRTAB] = 1;
	elf_append(&out, strtab.data, strtab.len);

	shoff[SH_ATTR] = (unsigned long)out.len;
	shsize[SH_ATTR] = (unsigned long)attr.len;
	shalign[SH_ATTR] = 1;
	elf_append(&out, attr.data, attr.len);

	shoff[SH_SHSTRTAB] = (unsigned long)out.len;
	shsize[SH_SHSTRTAB] = (unsigned long)shstrtab.len;
	shalign[SH_SHSTRTAB] = 1;
	elf_append(&out, shstrtab.data, shstrtab.len);

	buf_align(&out, 8);
	e_shoff = (unsigned long)out.len;

	/* e_ident and the ELF header. */
	out.data[0] = 0x7f;
	out.data[1] = 'E';
	out.data[2] = 'L';
	out.data[3] = 'F';
	out.data[4] = 2; /* ELFCLASS64 */
	out.data[5] = 1; /* ELFDATA2LSB */
	out.data[6] = 1; /* EV_CURRENT */
	out.data[7] = 0; /* ELFOSABI_NONE */
	for (i = 8; i < EI_NIDENT; i++)
		out.data[i] = 0;
	elf_put16(out.data + 16, ET_REL);
	elf_put16(out.data + 18, EM_RISCV);
	elf_put32(out.data + 20, 1); /* EV_CURRENT */
	elf_put64(out.data + 24, 0); /* e_entry */
	elf_put64(out.data + 32, 0); /* e_phoff */
	elf_put64(out.data + 40, e_shoff);
	elf_put32(out.data + 48, EF_RISCV_FLOAT_ABI_DOUBLE);
	elf_put16(out.data + 52, 64); /* e_ehsize */
	elf_put16(out.data + 54, 0); /* e_phentsize */
	elf_put16(out.data + 56, 0); /* e_phnum */
	elf_put16(out.data + 58, 64); /* e_shentsize */
	elf_put16(out.data + 60, SH_COUNT);
	elf_put16(out.data + 62, SH_SHSTRTAB);

	/* Section header table. */
	for (i = 0; i < SH_COUNT; i++) {
		unsigned int type = SHT_NULL;
		unsigned long flags = 0;
		unsigned int link = 0;
		unsigned int info = 0;
		unsigned long entsize = 0;

		switch (i) {
		case SH_TEXT:
			type = SHT_PROGBITS;
			flags = SHF_ALLOC | SHF_EXECINSTR;
			break;
		case SH_DATA:
			type = SHT_PROGBITS;
			flags = SHF_ALLOC | SHF_WRITE;
			break;
		case SH_BSS:
			type = SHT_NOBITS;
			flags = SHF_ALLOC | SHF_WRITE;
			break;
		case SH_RELA_TEXT:
			type = SHT_RELA;
			flags = SHF_INFO_LINK;
			link = SH_SYMTAB;
			info = SH_TEXT;
			entsize = 24;
			break;
		case SH_RELA_DATA:
			type = SHT_RELA;
			flags = SHF_INFO_LINK;
			link = SH_SYMTAB;
			info = SH_DATA;
			entsize = 24;
			break;
		case SH_SYMTAB:
			type = SHT_SYMTAB;
			link = SH_STRTAB;
			info = (unsigned int)first_global;
			entsize = 24;
			break;
		case SH_STRTAB:
		case SH_SHSTRTAB:
			type = SHT_STRTAB;
			break;
		case SH_ATTR:
			type = SHT_RISCV_ATTRIBUTES;
			break;
		default:
			break;
		}

		buf_u32(&out, shname[i]);
		buf_u32(&out, type);
		buf_u64(&out, flags);
		buf_u64(&out, 0); /* sh_addr */
		buf_u64(&out, shoff[i]);
		buf_u64(&out, shsize[i]);
		buf_u32(&out, link);
		buf_u32(&out, info);
		buf_u64(&out, shalign[i]);
		buf_u64(&out, entsize);
	}

	if (out.len > ELF_MAX_BYTES)
		fatal("ELF object too large");
	write_elf_bytes(path, out.data, out.len, 0644);

	free(arr);
	free(symidx);
	free(out.data);
	free(rela_text.data);
	free(rela_data.data);
	free(symtab.data);
	free(strtab.data);
	free(attr.data);
	free(shstrtab.data);
}

/* ------------------------------------------------------------------ */
/* ELF reader                                                          */
/* ------------------------------------------------------------------ */

struct elf_file {
	const unsigned char *p;
	unsigned long len;
	const char *path;
};

enum {
	ESEC_IGNORE = 0,
	ESEC_TEXT,
	ESEC_DATA,
	ESEC_BSS,
};

struct elf_section {
	unsigned int name;
	unsigned int type;
	unsigned long flags;
	unsigned long offset;
	unsigned long size;
	unsigned int link;
	unsigned int info;
	unsigned long align;
	unsigned long entsize;
	int kind;
	unsigned long base;
};

static void er_need(const struct elf_file *e, unsigned long off,
		    unsigned long n, const char *what)
{
	if (off > e->len || n > e->len - off)
		fatal("truncated ELF object '%s' (%s)", e->path, what);
}

static unsigned int er_u16(const struct elf_file *e, unsigned long off,
			   const char *what)
{
	er_need(e, off, 2, what);
	return (unsigned int)e->p[off] | ((unsigned int)e->p[off + 1] << 8);
}

static unsigned int er_u32(const struct elf_file *e, unsigned long off,
			   const char *what)
{
	er_need(e, off, 4, what);
	return (unsigned int)e->p[off] |
	       ((unsigned int)e->p[off + 1] << 8) |
	       ((unsigned int)e->p[off + 2] << 16) |
	       ((unsigned int)e->p[off + 3] << 24);
}

static unsigned long er_u64(const struct elf_file *e, unsigned long off,
			    const char *what)
{
	unsigned long v = 0;
	int i;

	er_need(e, off, 8, what);
	for (i = 0; i < 8; i++)
		v |= (unsigned long)e->p[off + (unsigned long)i] << (i * 8);
	return v;
}

static void er_check_section(const struct elf_file *e,
			     const struct elf_section *s, const char *name)
{
	if (s->type == SHT_NOBITS)
		return;
	er_need(e, s->offset, s->size, name);
}

static unsigned long er_align(unsigned long n, unsigned long a, const char *path)
{
	unsigned long pad;

	if (a == 0)
		a = 1;
	if (a > 4096 || (a & (a - 1)))
		fatal("bad section alignment in ELF object '%s'", path);
	pad = (a - n % a) % a;
	if (n > (unsigned long)INT_MAX - pad)
		fatal("section too large in ELF object '%s'", path);
	return n + pad;
}

static void er_append(struct buf *b, const unsigned char *p, unsigned long n,
		      const char *path)
{
	if (n > (unsigned long)INT_MAX || b->len > INT_MAX - (int)n)
		fatal("section too large in ELF object '%s'", path);
	buf_grow(b, (int)n);
	if (n)
		memcpy(b->data + b->len, p, (size_t)n);
	b->len += (int)n;
}

static int er_sec_kind(const struct elf_section *s, const char *name)
{
	if (!(s->flags & SHF_ALLOC))
		return ESEC_IGNORE;
	if (!strcmp(name, ".eh_frame") || !strncmp(name, ".eh_frame.", 10) ||
	    !strcmp(name, ".eh_frame_hdr") ||
	    !strcmp(name, ".gcc_except_table") ||
	    !strncmp(name, ".gcc_except_table.", 18))
		return ESEC_IGNORE;
	if (s->flags & SHF_EXECINSTR)
		return ESEC_TEXT;
	if (s->type == SHT_NOBITS)
		return ESEC_BSS;
	return ESEC_DATA;
}

static int er_kind_sec(int kind)
{
	switch (kind) {
	case ESEC_TEXT:
		return SEC_TEXT;
	case ESEC_DATA:
		return SEC_DATA;
	case ESEC_BSS:
		return SEC_BSS;
	default:
		fatal("internal error: bad ELF section kind");
		return 0;
	}
}

struct unit *read_elf_object(const char *path, const unsigned char *buf,
			     int len)
{
	struct elf_file e;
	struct elf_section *secs;
	struct unit *u;
	struct sym *tail = NULL;
	struct sym **byidx;
	struct sym **secsym;
	struct buf textbuf;
	struct buf databuf;
	unsigned long shoff;
	unsigned long bss_len = 0;
	unsigned int nsecs;
	unsigned int shentsize;
	unsigned int shstrndx;
	int symtab_sec = -1;
	int strtab_sec = -1;
	unsigned long shstr_size = 0;
	const char *shstr = "";
	unsigned int nsyms = 0;
	unsigned int eflags = 0;
	unsigned int i;

	memset(&e, 0, sizeof(e));
	e.p = buf;
	e.len = (unsigned long)len;
	e.path = path;

	if (len < 64 || buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' ||
	    buf[3] != 'F')
		fatal("'%s' is not an ELF object", path);
	if (buf[4] != 2 || buf[5] != 1 || buf[6] != 1)
		fatal("unsupported ELF class/encoding/version in '%s'", path);
	if (er_u16(&e, 16, "e_type") != ET_REL)
		fatal("'%s' is not a relocatable ELF object", path);
	if (er_u16(&e, 18, "e_machine") != EM_RISCV)
		fatal("'%s' is not a RISC-V ELF object", path);
	if (er_u32(&e, 20, "e_version") != 1)
		fatal("unsupported ELF version in '%s'", path);
	eflags = er_u32(&e, 48, "e_flags");

	shoff = er_u64(&e, 40, "e_shoff");
	shentsize = er_u16(&e, 58, "e_shentsize");
	nsecs = er_u16(&e, 60, "e_shnum");
	shstrndx = er_u16(&e, 62, "e_shstrndx");
	if (nsecs == 0)
		fatal("ELF object '%s' has no section header table", path);
	if (shentsize != 64)
		fatal("bad section header size in ELF object '%s'", path);
	if (nsecs > 100000)
		fatal("too many sections in ELF object '%s'", path);
	if (shoff > e.len ||
	    (unsigned long)nsecs > (e.len - shoff) / 64)
		fatal("truncated section header table in ELF object '%s'", path);
	if (shstrndx == 0 || shstrndx >= nsecs)
		fatal("bad section name table index in ELF object '%s'", path);

	secs = xcalloc(nsecs, sizeof(*secs));
	for (i = 0; i < nsecs; i++) {
		unsigned long o = shoff + (unsigned long)i * 64;

		secs[i].name = er_u32(&e, o + 0, "section name");
		secs[i].type = er_u32(&e, o + 4, "section type");
		secs[i].flags = er_u64(&e, o + 8, "section flags");
		secs[i].offset = er_u64(&e, o + 24, "section offset");
		secs[i].size = er_u64(&e, o + 32, "section size");
		secs[i].link = er_u32(&e, o + 40, "section link");
		secs[i].info = er_u32(&e, o + 44, "section info");
		secs[i].align = er_u64(&e, o + 48, "section alignment");
		secs[i].entsize = er_u64(&e, o + 56, "section entry size");
	}

	{
		const struct elf_section *sh = &secs[shstrndx];

		if (sh->type != SHT_STRTAB)
			fatal("section name table is not a string table in '%s'",
			      path);
		er_check_section(&e, sh, ".shstrtab");
		shstr_size = sh->size;
		shstr = (const char *)(buf + sh->offset);
	}

	for (i = 1; i < nsecs; i++) {
		const char *name;

		if (secs[i].name >= shstr_size)
			fatal("bad section name offset in ELF object '%s'",
			      path);
		name = shstr + secs[i].name;
		if (!memchr(name, '\0', (size_t)(shstr_size - secs[i].name)))
			fatal("unterminated section name in ELF object '%s'",
			      path);
		if (!strcmp(name, ".symtab"))
			symtab_sec = (int)i;
		else if (!strcmp(name, ".strtab"))
			strtab_sec = (int)i;
	}
	if (symtab_sec < 0 || strtab_sec < 0)
		fatal("ELF object '%s' is missing a symbol table", path);
	if (secs[symtab_sec].type != SHT_SYMTAB ||
	    secs[symtab_sec].entsize != 24 ||
	    secs[symtab_sec].size % 24)
		fatal("bad symbol table in ELF object '%s'", path);
	if (secs[strtab_sec].type != SHT_STRTAB)
		fatal("bad string table in ELF object '%s'", path);
	er_check_section(&e, &secs[strtab_sec], ".strtab");
	if (secs[symtab_sec].link != (unsigned int)strtab_sec)
		fatal("symbol table does not reference .strtab in '%s'", path);

	buf_init(&textbuf);
	buf_init(&databuf);
	u = xcalloc(1, sizeof(*u));
	secsym = xcalloc(nsecs, sizeof(*secsym));
	for (i = 1; i < nsecs; i++) {
		struct elf_section *s = &secs[i];
		const char *name = shstr + s->name;
		unsigned long align;

		s->kind = er_sec_kind(s, name);
		if (s->kind == ESEC_IGNORE)
			continue;
		er_check_section(&e, s, name);
		align = s->align;
		if (align == 0)
			align = 1;
		if (align > 4096 || (align & (align - 1)))
			fatal("bad section alignment in ELF object '%s'", path);
		switch (s->kind) {
		case ESEC_TEXT:
			buf_align(&textbuf, (int)align);
			s->base = (unsigned long)textbuf.len;
			er_append(&textbuf, buf + s->offset, s->size, path);
			break;
		case ESEC_DATA:
			buf_align(&databuf, (int)align);
			s->base = (unsigned long)databuf.len;
			er_append(&databuf, buf + s->offset, s->size, path);
			break;
		case ESEC_BSS:
			bss_len = er_align(bss_len, align, path);
			if (s->size > (unsigned long)INT_MAX - bss_len)
				fatal("section too large in ELF object '%s'",
				      path);
			s->base = bss_len;
			bss_len += s->size;
			break;
		default:
			break;
		}
		if (s->kind != ESEC_IGNORE) {
			struct sym *ssym = xcalloc(1, sizeof(*ssym));

			ssym->name = xstrdup(name);
			ssym->sec = er_kind_sec(s->kind);
			ssym->defined = true;
			ssym->is_static = true;
			ssym->offset = (int)s->base;
			ssym->size = (int)s->size;
			ssym->align = (int)align;
			if (ssym->align < 1 || ssym->align > 4096 ||
			    (ssym->align & (ssym->align - 1)))
				ssym->align = 1;
			secsym[i] = ssym;
			if (tail)
				tail->next = ssym;
			else
				u->syms = ssym;
			tail = ssym;
		}
	}

	nsyms = (unsigned int)(secs[symtab_sec].size / 24);
	if (nsyms == 0)
		fatal("ELF object '%s' has an empty symbol table", path);
	byidx = xcalloc(nsyms, sizeof(*byidx));

	u->code = textbuf.data;
	u->code_len = textbuf.len;
	u->data = databuf.data;
	u->data_len = databuf.len;
	u->bss_len = (int)bss_len;
	u->uses_rvc = (eflags & 1) != 0;

	{
		const struct elf_section *st = &secs[symtab_sec];
		const struct elf_section *str = &secs[strtab_sec];
		const char *strp = (const char *)(buf + str->offset);
		unsigned long strsz = str->size;

		for (i = 1; i < nsyms; i++) {
			unsigned long o = st->offset + (unsigned long)i * 24;
			unsigned int nameoff = er_u32(&e, o + 0, "st_name");
			unsigned int info = e.p[o + 4];
			unsigned int bind = info >> 4;
			unsigned int type = info & 0xf;
			unsigned int shndx = er_u16(&e, o + 6, "st_shndx");
			unsigned long value = er_u64(&e, o + 8, "st_value");
			unsigned long size = er_u64(&e, o + 16, "st_size");
			const char *name;
			struct sym *s;

			if (type == STT_SECTION) {
				if (shndx < nsecs && secsym[shndx])
					byidx[i] = secsym[shndx];
				continue;
			}
			if (type == STT_FILE)
				continue;
			if (shndx == SHN_ABS)
				continue;
			if (nameoff >= strsz)
				fatal("bad symbol name offset in ELF object '%s'",
				      path);
			name = strp + nameoff;
			if (!memchr(name, '\0', (size_t)(strsz - nameoff)))
				fatal("unterminated symbol name in ELF object '%s'",
				      path);
			if (bind != STB_LOCAL && bind != STB_GLOBAL)
				fatal("unsupported symbol binding for '%s' in '%s'",
				      name, path);

			s = xcalloc(1, sizeof(*s));
			s->name = xstrdup(name);
			s->is_static = bind == STB_LOCAL;
			if (shndx == SHN_UNDEF) {
				s->defined = false;
				s->sec = type == STT_FUNC ? SEC_TEXT : SEC_DATA;
				s->offset = 0;
				s->size = 0;
				s->align = 8;
			} else if (shndx == SHN_COMMON) {
				fatal("common symbol '%s' in '%s' is not supported",
				      name, path);
			} else if (shndx >= nsecs) {
				fatal("symbol '%s' in '%s' has an unsupported section index",
				      name, path);
			} else {
				const struct elf_section *ss = &secs[shndx];

				if (ss->kind == ESEC_IGNORE)
					continue;
				if (value > ss->size ||
				    size > ss->size - value)
					fatal("symbol '%s' is outside its section in '%s'",
					      name, path);
				if (ss->base + value >
				    (unsigned long)INT_MAX - size)
					fatal("symbol offset too large in '%s'",
					      path);
				s->defined = true;
				s->sec = er_kind_sec(ss->kind);
				s->offset = (int)(ss->base + value);
				s->size = (int)size;
				s->align = (int)(ss->align ?
							 ss->align : 1);
				if (s->align < 1 || s->align > 4096 ||
				    (s->align & (s->align - 1)))
					s->align = 1;
			}
			byidx[i] = s;
			if (tail)
				tail->next = s;
			else
				u->syms = s;
			tail = s;
		}
	}

	for (i = 1; i < nsecs; i++) {
		const struct elf_section *rs = &secs[i];
		unsigned int tgt;
		unsigned long count;
		unsigned long j;

		if (rs->type != SHT_RELA)
			continue;
		if (rs->info >= nsecs)
			fatal("bad relocation target section in ELF object '%s'",
			      path);
		tgt = rs->info;
		if (secs[tgt].kind == ESEC_IGNORE)
			continue;
		if (rs->entsize != 24 || rs->size % 24 ||
		    rs->link != (unsigned int)symtab_sec)
			fatal("bad relocation section in ELF object '%s'", path);
		er_check_section(&e, rs, "relocation section");
		count = rs->size / 24;
		for (j = 0; j < count; j++) {
			unsigned long o = rs->offset + j * 24;
			unsigned long off = er_u64(&e, o, "r_offset");
			unsigned long info =
				er_u64(&e, o + 8, "r_info");
			unsigned int type =
				(unsigned int)(info & 0xffffffffu);
			unsigned int target = (unsigned int)(info >> 32);
			long addend = (long)er_u64(&e, o + 16, "r_addend");

			if (type == R_RISCV_RELAX || type == R_RISCV_ALIGN)
				continue;
			if (target >= nsyms || !byidx[target])
				fatal("relocation target %u (type %u, section %s) is not a symbol in ELF object '%s'",
				      target, type,
				      shstr + secs[tgt].name, path);

			if (secs[tgt].kind == ESEC_TEXT) {
				unsigned long isz =
					(type == R_RISCV_RVC_BRANCH ||
					 type == R_RISCV_RVC_JUMP) ? 2 : 4;
				struct reloc *r;
				int kind;

				if (off > secs[tgt].size ||
				    secs[tgt].size - off < isz)
					fatal("text relocation out of range in ELF object '%s'",
					      path);
				switch (type) {
				case R_RISCV_HI20:
					kind = R_HI20;
					break;
				case R_RISCV_LO12_I:
					kind = R_LO12I;
					break;
				case R_RISCV_LO12_S:
					kind = R_LO12S;
					break;
				case R_RISCV_JAL:
					kind = R_JAL;
					break;
				case R_RISCV_BRANCH:
					kind = R_BRANCH;
					break;
				case R_RISCV_CALL:
				case R_RISCV_CALL_PLT:
					kind = R_CALL;
					break;
				case R_RISCV_PCREL_HI20:
					kind = R_PCREL_HI20;
					break;
				case R_RISCV_PCREL_LO12_I:
					kind = R_PCREL_LO12I;
					break;
				case R_RISCV_PCREL_LO12_S:
					kind = R_PCREL_LO12S;
					break;
				case R_RISCV_RVC_BRANCH:
					kind = R_RVC_BRANCH;
					break;
				case R_RISCV_RVC_JUMP:
					kind = R_RVC_JUMP;
					break;
				default:
					fatal("unsupported RISC-V text relocation %u in ELF object '%s'",
					      type, path);
				}
				r = xcalloc(1, sizeof(*r));
				r->pos = (int)(secs[tgt].base + off);
				r->kind = kind;
				r->sym = byidx[target];
				r->addend = addend;
				r->next = u->relocs;
				u->relocs = r;
			} else if (secs[tgt].kind == ESEC_DATA) {
				int size;
				int kind;

				switch (type) {
				case R_RISCV_32:
					size = 4;
					kind = DR_ABS;
					break;
				case R_RISCV_64:
					size = 8;
					kind = DR_ABS;
					break;
				case R_RISCV_32_PCREL:
					size = 4;
					kind = DR_PCREL;
					break;
				case R_RISCV_ADD8:
					size = 1;
					kind = DR_ADD;
					break;
				case R_RISCV_ADD16:
					size = 2;
					kind = DR_ADD;
					break;
				case R_RISCV_ADD32:
					size = 4;
					kind = DR_ADD;
					break;
				case R_RISCV_ADD64:
					size = 8;
					kind = DR_ADD;
					break;
				case R_RISCV_SUB8:
					size = 1;
					kind = DR_SUB;
					break;
				case R_RISCV_SUB16:
					size = 2;
					kind = DR_SUB;
					break;
				case R_RISCV_SUB32:
					size = 4;
					kind = DR_SUB;
					break;
				case R_RISCV_SUB64:
					size = 8;
					kind = DR_SUB;
					break;
				default:
					fatal("unsupported RISC-V data relocation %u in ELF object '%s'",
					      type, path);
				}
				if (off > secs[tgt].size ||
				    secs[tgt].size - off <
					    (unsigned long)size)
					fatal("data relocation out of range in ELF object '%s'",
					      path);
				{
					unsigned long at = secs[tgt].base + off;
					struct sym *owner = NULL;
					struct sym *sym2;
					struct data_reloc *dr;

					for (sym2 = u->syms; sym2;
					     sym2 = sym2->next) {
						if (!sym2->defined ||
						    sym2->sec != SEC_DATA ||
						    sym2->size < size)
							continue;
						if (at >=
							    (unsigned long)sym2->offset &&
						    at + (unsigned long)size <=
							    (unsigned long)sym2->offset +
								    (unsigned long)sym2->size) {
							owner = sym2;
							break;
						}
					}
					if (!owner)
						fatal("data relocation is not inside a data symbol in ELF object '%s'",
						      path);
					dr = xcalloc(1, sizeof(*dr));
					dr->offset = (int)(at -
							   (unsigned long)owner->offset);
					dr->size = size;
					dr->kind = kind;
					dr->sym = byidx[target];
					dr->addend = addend;
					dr->next = owner->drel;
					owner->drel = dr;
				}
			}
		}
	}

	/* Section symbol names are not repeated in the global string table. */
	free(byidx);
	free(secsym);
	free(secs);
	if (unit_tail)
		unit_tail->next = u;
	else
		units = u;
	unit_tail = u;
	return u;
}
