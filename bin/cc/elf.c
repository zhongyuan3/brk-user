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

#define R_RISCV_64 2
#define R_RISCV_JAL 17
#define R_RISCV_HI20 26
#define R_RISCV_LO12_I 27
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

			if (dr->offset < 0 || s->size < 8 ||
			    dr->offset > s->size - 8)
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
			elf_rela(&rela_data, at, R_RISCV_64,
				 (unsigned int)idx, dr->addend);
		}
	}

	text_align = 4;
	data_align = elf_section_align(u, SEC_DATA);
	bss_align = elf_section_align(u, SEC_BSS);

	buf_init(&shstrtab);
	buf_byte(&shstrtab, 0);
	{
		static const char *const names[SH_COUNT] = {
			"", ".text", ".data", ".bss", ".rela.text",
			".rela.data", ".symtab", ".strtab", ".shstrtab",
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

struct unit *read_elf_object(const char *path, const unsigned char *buf,
			     int len)
{
	struct elf_file e;
	struct elf_section *secs;
	struct unit *u;
	struct sym *tail = NULL;
	struct sym **byidx;
	unsigned long shoff;
	unsigned int nsecs;
	unsigned int shentsize;
	unsigned int shstrndx;
	int text_sec = -1;
	int data_sec = -1;
	int bss_sec = -1;
	int symtab_sec = -1;
	int strtab_sec = -1;
	int rela_text_sec = -1;
	int rela_data_sec = -1;
	unsigned long shstr_off = 0;
	unsigned long shstr_size = 0;
	const char *shstr = "";
	unsigned int nsyms = 0;
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
		shstr_off = sh->offset;
		shstr_size = sh->size;
		shstr = (const char *)(buf + shstr_off);
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
		if (!strcmp(name, ".text"))
			text_sec = (int)i;
		else if (!strcmp(name, ".data"))
			data_sec = (int)i;
		else if (!strcmp(name, ".bss"))
			bss_sec = (int)i;
		else if (!strcmp(name, ".symtab"))
			symtab_sec = (int)i;
		else if (!strcmp(name, ".strtab"))
			strtab_sec = (int)i;
		else if (!strcmp(name, ".rela.text"))
			rela_text_sec = (int)i;
		else if (!strcmp(name, ".rela.data"))
			rela_data_sec = (int)i;
	}

	if (text_sec >= 0)
		er_check_section(&e, &secs[text_sec], ".text");
	if (data_sec >= 0)
		er_check_section(&e, &secs[data_sec], ".data");
	if (bss_sec >= 0 && secs[bss_sec].type != SHT_NOBITS)
		fatal(".bss is not SHT_NOBITS in ELF object '%s'", path);
	if (text_sec < 0 || data_sec < 0 || bss_sec < 0)
		fatal("ELF object '%s' is missing .text/.data/.bss", path);
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

	nsyms = (unsigned int)(secs[symtab_sec].size / 24);
	if (nsyms == 0)
		fatal("ELF object '%s' has an empty symbol table", path);
	byidx = xcalloc(nsyms, sizeof(*byidx));

	if (secs[text_sec].size > (unsigned long)INT_MAX ||
	    secs[data_sec].size > (unsigned long)INT_MAX ||
	    secs[bss_sec].size > (unsigned long)INT_MAX)
		fatal("section too large in ELF object '%s'", path);
	u = xcalloc(1, sizeof(*u));
	u->code_len = (int)secs[text_sec].size;
	u->data_len = (int)secs[data_sec].size;
	u->bss_len = (int)secs[bss_sec].size;
	u->code = xmalloc((size_t)(u->code_len > 0 ? u->code_len : 1));
	if (u->code_len)
		memcpy(u->code, buf + secs[text_sec].offset,
		       (size_t)u->code_len);
	u->data = xmalloc((size_t)(u->data_len > 0 ? u->data_len : 1));
	if (u->data_len)
		memcpy(u->data, buf + secs[data_sec].offset,
		       (size_t)u->data_len);

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

			if (type == STT_SECTION)
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
			} else if (shndx == SHN_ABS) {
				fatal("absolute symbol '%s' in '%s' is not supported",
				      name, path);
			} else if (shndx == SHN_COMMON) {
				fatal("common symbol '%s' in '%s' is not supported",
				      name, path);
			} else {
				const struct elf_section *ss;

				if ((int)shndx == text_sec)
					s->sec = SEC_TEXT;
				else if ((int)shndx == data_sec)
					s->sec = SEC_DATA;
				else if ((int)shndx == bss_sec)
					s->sec = SEC_BSS;
				else
					fatal("symbol '%s' in '%s' has an unsupported section index",
					      name, path);
				ss = &secs[shndx];
				if (value > ss->size ||
				    size > ss->size - value)
					fatal("symbol '%s' is outside its section in '%s'",
					      name, path);
				s->defined = true;
				s->offset = (int)value;
				s->size = (int)size;
				s->align = 1;
				if (ss->align > 0 && (ss->align & (ss->align - 1)) == 0)
					s->align = (int)ss->align;
				if (s->align < 1 || s->align > 4096)
					s->align = 8;
			}
			byidx[i] = s;
			if (tail)
				tail->next = s;
			else
				u->syms = s;
			tail = s;
		}
	}

	if (rela_text_sec >= 0) {
		const struct elf_section *rs = &secs[rela_text_sec];

		if (rs->type != SHT_RELA || rs->entsize != 24 ||
		    rs->size % 24 || rs->link != (unsigned int)symtab_sec ||
		    rs->info != (unsigned int)text_sec)
			fatal("bad .rela.text in ELF object '%s'", path);
		er_check_section(&e, rs, ".rela.text");
		for (i = 0; i < rs->size / 24; i++) {
			unsigned long o = rs->offset + (unsigned long)i * 24;
			unsigned long off = er_u64(&e, o, "r_offset");
			unsigned long info = er_u64(&e, o + 8, "r_info");
			unsigned int type = (unsigned int)(info & 0xffffffffu);
			unsigned int target = (unsigned int)(info >> 32);
			long addend = (long)er_u64(&e, o + 16, "r_addend");
			struct reloc *r;

			if (off > (unsigned long)u->code_len ||
			    (unsigned long)u->code_len - off < 4)
				fatal("text relocation out of range in ELF object '%s'",
				      path);
			if (off % 4)
				fatal("unaligned text relocation in ELF object '%s'",
				      path);
			if (type == R_RISCV_RELAX)
				continue;
			if (target >= nsyms || !byidx[target])
				fatal("text relocation target is not a symbol in ELF object '%s'",
				      path);
			r = xcalloc(1, sizeof(*r));
			r->pos = (int)off;
			r->sym = byidx[target];
			r->addend = addend;
			switch (type) {
			case R_RISCV_HI20:
				r->kind = R_HI20;
				break;
			case R_RISCV_LO12_I:
				r->kind = R_LO12I;
				break;
			case R_RISCV_JAL:
				r->kind = R_JAL;
				break;
			default:
				fatal("unsupported RISC-V text relocation %u in ELF object '%s'",
				      type, path);
			}
			r->next = u->relocs;
			u->relocs = r;
		}
	}

	if (rela_data_sec >= 0) {
		const struct elf_section *rs = &secs[rela_data_sec];

		if (rs->type != SHT_RELA || rs->entsize != 24 ||
		    rs->size % 24 || rs->link != (unsigned int)symtab_sec ||
		    rs->info != (unsigned int)data_sec)
			fatal("bad .rela.data in ELF object '%s'", path);
		er_check_section(&e, rs, ".rela.data");
		for (i = 0; i < rs->size / 24; i++) {
			unsigned long o = rs->offset + (unsigned long)i * 24;
			unsigned long off = er_u64(&e, o, "r_offset");
			unsigned long info = er_u64(&e, o + 8, "r_info");
			unsigned int type = (unsigned int)(info & 0xffffffffu);
			unsigned int target = (unsigned int)(info >> 32);
			long addend = (long)er_u64(&e, o + 16, "r_addend");
			struct sym *owner = NULL;
			struct sym *s;
			struct data_reloc *dr;

			if (off > (unsigned long)u->data_len ||
			    (unsigned long)u->data_len - off < 8)
				fatal("data relocation out of range in ELF object '%s'",
				      path);
			if (type != R_RISCV_64)
				fatal("unsupported RISC-V data relocation %u in ELF object '%s'",
				      type, path);
			if (target >= nsyms || !byidx[target])
				fatal("data relocation target is not a symbol in ELF object '%s'",
				      path);
			for (s = u->syms; s; s = s->next) {
				if (!s->defined || s->sec != SEC_DATA ||
				    s->size < 8)
					continue;
				if (off >= (unsigned long)s->offset &&
				    off + 8 <=
					    (unsigned long)s->offset +
						    (unsigned long)s->size) {
					owner = s;
					break;
				}
			}
			if (!owner)
				fatal("data relocation is not inside a data symbol in ELF object '%s'",
				      path);
			dr = xcalloc(1, sizeof(*dr));
			dr->offset = (int)(off - (unsigned long)owner->offset);
			dr->sym = byidx[target];
			dr->addend = addend;
			dr->next = owner->drel;
			owner->drel = dr;
		}
	}

	free(byidx);
	free(secs);
	if (unit_tail)
		unit_tail->next = u;
	else
		units = u;
	unit_tail = u;
	return u;
}
