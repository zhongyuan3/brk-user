/*
 * ld-brk.so - the BRK dynamic linker.
 *
 * Freestanding: raw syscalls only, no libc, its own bump allocator.
 * The kernel maps both the interpreter and the main image and has
 * already applied every R_RISCV_RELATIVE of the interpreter (the
 * build keeps all of its symbols hidden and binds them locally), so
 * this program needs no self-relocation.
 *
 * Startup contract: a0/a1/a2 carry argc/argv/envp (the register
 * convention of BRK crt) and the full SysV stack (argc, argv, NULL,
 * envp, NULL, auxv) sits on sp.  After loading and relocating the
 * DT_NEEDED chain of the main image - the symbol work the kernel
 * refuses to do - registers and stack are handed to the main entry
 * point from auxv untouched.
 */

typedef unsigned long ulong;
typedef unsigned int u32;
typedef unsigned short u16;

#include <uapi/auxv.h>
#include <uapi/elf.h>
#include <uapi/fcntl.h>
#include <uapi/mman.h>
#include <uapi/syscall.h>

#define NULL ((void *)0)

#define LD_PAGE 4096UL
#define LD_FILE_CAP (1UL << 20)
#define LD_POOL_CAP (2UL << 20)
#define LD_MAX_MODS 16
#define LD_LIB_BASE 0x2000000000UL	/* 128 GB, below the stack zone */
#define LD_LIB_GAP (1UL << 20)

#define R_RISCV_64 2
#define R_RISCV_JUMP_SLOT 5
#define R_RISCV_GLOB_DAT 6
#define R_RISCV_ALIGN 52

struct ld_sym {
	u32 st_name;
	unsigned char st_info;
	unsigned char st_other;
	u16 st_shndx;
	ulong st_value;
	ulong st_size;
};

struct ld_hash_head {
	u32 nbucket;
	u32 nchain;
};

struct ld_mod {
	const char *name;
	ulong bias;
	struct __brk_elf_dyn *dyn;
	ulong strtab, symtab, hash;
	ulong strsz;
	ulong rela, relasz, relaent;
	ulong jmprel, pltrelsz;
	ulong init, init_array, init_arraysz;
	struct ld_mod *next;
};

static struct ld_mod *ld_mods;	/* load order: exe first, newest last */
static unsigned char ld_pool[LD_POOL_CAP] __attribute__((aligned(16)));
static unsigned long ld_pool_pos;
static unsigned long ld_lib_cursor = LD_LIB_BASE;

static void ld_die(const char *msg);
static void ld_load_library(const char *name);

/* ---- freestanding basics (gcc may emit calls to these) ---- */

static void *ld_memcpy(void *dst, const void *src, ulong n)
{
	char *d = dst;
	const char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

static void *ld_memset(void *dst, int c, ulong n)
{
	char *d = dst;

	while (n--)
		*d++ = (char)c;
	return dst;
}

static ulong ld_strlen(const char *s)
{
	ulong n = 0;

	while (s[n])
		++n;
	return n;
}

static int ld_streq(const char *a, const char *b)
{
	while (*a && *a == *b)
		++a, ++b;
	return *a == *b;
}

static void *ld_alloc(ulong n)
{
	void *p;

	n = (n + 15UL) & ~15UL;
	if (ld_pool_pos + n > LD_POOL_CAP)
		ld_die("ld-brk: out of memory\n");
	p = ld_pool + ld_pool_pos;
	ld_pool_pos += n;
	return p;
}

/* ---- raw syscall layer ---- */

static long ld_syscall6(long n, long a, long b, long c, long d, long e,
		       long f)
{
	register long a0 asm("a0") = a;
	register long a1 asm("a1") = b;
	register long a2 asm("a2") = c;
	register long a3 asm("a3") = d;
	register long a4 asm("a4") = e;
	register long a5 asm("a5") = f;
	register long a7 asm("a7") = n;

	asm volatile("ecall"
		     : "+r"(a0)
		     : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),
		       "r"(a7)
		     : "memory");
	return a0;
}

static long ld_open(const char *path)
{
	/* the ABI is openat(dirfd, path, flags, mode) */
	return ld_syscall6(SYS_OPEN, AT_FDCWD, (long)path, O_RDONLY, 0, 0,
			   0);
}

static long ld_read(int fd, void *buf, ulong len)
{
	return ld_syscall6(SYS_READ, fd, (long)buf, (long)len, 0, 0, 0);
}

static long ld_close(int fd)
{
	return ld_syscall6(SYS_CLOSE, fd, 0, 0, 0, 0, 0);
}

static void ld_write_str(const char *s)
{
	ld_syscall6(SYS_WRITE, 2, (long)s, (long)ld_strlen(s), 0, 0, 0);
}

static void ld_die(const char *msg)
{
	ld_write_str(msg);
	for (;;)
		ld_syscall6(SYS_EXIT_ALL, 127, 0, 0, 0, 0, 0);
}

static void ld_mmap_fixed(ulong addr, ulong len, int prot)
{
	long r = ld_syscall6(SYS_MMAP, (long)addr, (long)len, prot,
			     MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (r < 0)
		ld_die("ld-brk: mmap failed\n");
}

static void ld_mprotect(ulong addr, ulong len, int prot)
{
	ld_syscall6(SYS_MPROTECT, (long)addr, (long)len, prot, 0, 0, 0);
}

/* ---- module bookkeeping ---- */

static struct ld_mod *ld_mod_by_name(const char *name)
{
	struct ld_mod *m;

	for (m = ld_mods; m; m = m->next)
		if (ld_streq(m->name, name))
			return m;
	return NULL;
}

static ulong ld_hash_str(const char *s)
{
	u32 h = 0;

	while (*s) {
		u32 c = (unsigned char)*s++;
		u32 hi;

		h = (h << 4) + c;
		hi = h & 0xf0000000u;
		if (hi)
			h ^= hi >> 24;
		h &= ~hi;
	}
	return h;
}

/* SysV DT_HASH lookup in one module; returns the biased address. */
static ulong ld_lookup_local(const struct ld_mod *m, const char *name)
{
	const struct ld_hash_head *hh;
	const u32 *bucket, *chain;
	ulong h, ndx;

	if (!m->hash || !m->symtab || !m->strtab)
		return 0;
	hh = (const struct ld_hash_head *)m->hash;
	bucket = (const u32 *)(m->hash + sizeof(*hh));
	chain = bucket + hh->nbucket;
	h = ld_hash_str(name);
	for (ndx = bucket[h % hh->nbucket]; ndx != 0; ndx = chain[ndx]) {
		const struct ld_sym *sym =
			&((const struct ld_sym *)m->symtab)[ndx];

		if (sym->st_name < m->strsz &&
		    ld_streq(name, (const char *)m->strtab + sym->st_name)) {
			/*
			 * The executable's dynsym also carries the
			 * undefined references to imported symbols;
			 * a name match on SHN_UNDEF is not a
			 * definition - keep searching the scope.
			 */
			if (sym->st_shndx == 0)
				return 0;
			return m->bias + sym->st_value;
		}
	}
	return 0;
}

/*
 * Global search: the executable first (its symbols win), then every
 * loaded module in load order.
 */
static ulong ld_lookup_global(const char *name)
{
	struct ld_mod *m;
	ulong a;

	for (m = ld_mods; m; m = m->next)
		if ((a = ld_lookup_local(m, name)))
			return a;
	return 0;
}

/* ---- file loading ---- */

struct ld_file {
	unsigned char *buf;
	ulong len;
};

static int ld_slurp(const char *path, struct ld_file *out)
{
	long fd = ld_open(path);
	ulong got = 0;

	if (fd < 0)
		return -1;
	out->buf = ld_alloc(LD_FILE_CAP);
	for (;;) {
		long n = ld_read((int)fd, out->buf + got, LD_FILE_CAP - got);

		if (n < 0) {
			ld_close((int)fd);
			return -1;
		}
		if (n == 0)
			break;
		got += (ulong)n;
		if (got >= LD_FILE_CAP) {
			ld_close((int)fd);
			return -1;
		}
	}
	ld_close((int)fd);
	out->len = got;
	return 0;
}

static const struct __brk_elf_hdr *ld_file_hdr(const struct ld_file *f)
{
	const struct __brk_elf_hdr *h = (const struct __brk_elf_hdr *)f->buf;

	if (f->len < sizeof(*h))
		ld_die("ld-brk: short ELF\n");
	if (h->e_ident[0] != 0x7f || h->e_ident[1] != 'E' ||
	    h->e_ident[2] != 'L' || h->e_ident[3] != 'F' ||
	    h->e_ident[4] != ELFCLASS64 || h->e_machine != EM_RISCV ||
	    h->e_phentsize != sizeof(struct __brk_elf_phdr) ||
	    h->e_phnum == 0 || h->e_phnum > 256 ||
	    (ulong)h->e_phoff + (ulong)h->e_phnum * sizeof(*h) > f->len)
		ld_die("ld-brk: bad ELF\n");
	return h;
}

/*
 * Map a DSO: fresh anonymous pages at a descending base, segments
 * copied in, non-writable segments sealed with mprotect.  The new
 * module lands at the tail of the load list.
 */
static void ld_map_module(const char *name, const struct ld_file *f)
{
	const struct __brk_elf_hdr *h = ld_file_hdr(f);
	const struct __brk_elf_phdr *ph =
		(const struct __brk_elf_phdr *)(f->buf + h->e_phoff);
	ulong link_base = (ulong)-1;
	ulong top = 0;
	struct ld_mod **tail;
	struct ld_mod *m;
	int i;

	for (i = 0; i < h->e_phnum; ++i) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		if (ph[i].p_vaddr < link_base)
			link_base = ph[i].p_vaddr;
		if (ph[i].p_vaddr + ph[i].p_memsz > top)
			top = ph[i].p_vaddr + ph[i].p_memsz;
	}
	if (link_base == (ulong)-1)
		ld_die("ld-brk: no loadable segments\n");

	ld_lib_cursor -= ((top + LD_PAGE - 1) & ~(LD_PAGE - 1)) + LD_LIB_GAP;
	ld_lib_cursor &= ~(LD_PAGE - 1);

	m = ld_alloc(sizeof(*m));
	ld_memset(m, 0, sizeof(*m));
	m->name = name;
	m->bias = ld_lib_cursor - link_base;

	for (i = 0; i < h->e_phnum; ++i) {
		ulong vaddr, start, end;
		int prot;

		if (ph[i].p_type != PT_LOAD)
			continue;
		vaddr = m->bias + ph[i].p_vaddr;
		start = vaddr & ~(LD_PAGE - 1);
		end = (vaddr + ph[i].p_memsz + LD_PAGE - 1) & ~(LD_PAGE - 1);
		prot = PROT_READ | PROT_WRITE |
		      ((ph[i].p_flags & PF_X) ? PROT_EXEC : 0);
		ld_mmap_fixed(start, end - start, prot);
		ld_memcpy((void *)vaddr, f->buf + ph[i].p_offset,
			  ph[i].p_filesz);
		/* beyond p_filesz the pages are fresh anonymous zero */
		if (!(ph[i].p_flags & PF_W)) {
			prot = PROT_READ |
			       ((ph[i].p_flags & PF_X) ? PROT_EXEC : 0);
			ld_mprotect(start, end - start, prot);
		}
	}

	for (i = 0; i < h->e_phnum; ++i)
		if (ph[i].p_type == PT_DYNAMIC) {
			m->dyn = (struct __brk_elf_dyn *)(m->bias +
							  ph[i].p_vaddr);
			break;
		}
	if (!m->dyn)
		ld_die("ld-brk: DSO without PT_DYNAMIC\n");

	tail = &ld_mods;
	while (*tail)
		tail = &(*tail)->next;
	*tail = m;
}

/*
 * Fold a PT_DYNAMIC into a module record, then pull in every
 * DT_NEEDED dependency before anything is relocated.
 */
static void ld_parse_dynamic(struct ld_mod *m)
{
	struct __brk_elf_dyn *d = m->dyn;
	struct __brk_elf_dyn *needed[LD_MAX_MODS];
	int nneeded = 0;
	int i;

	for (i = 0; d[i].d_tag != DT_NULL; ++i) {
		switch ((u32)d[i].d_tag) {
		case DT_NEEDED:
			if (nneeded < LD_MAX_MODS)
				needed[nneeded++] = &d[i];
			break;
		case DT_STRTAB:
			m->strtab = m->bias + d[i].d_un.d_ptr;
			break;
		case DT_SYMTAB:
			m->symtab = m->bias + d[i].d_un.d_ptr;
			break;
		case DT_STRSZ:
			m->strsz = d[i].d_un.d_val;
			break;
		case DT_SYMENT:
			if (d[i].d_un.d_val != sizeof(struct ld_sym))
				ld_die("ld-brk: bad DT_SYMENT\n");
			break;
		case DT_HASH:
			m->hash = m->bias + d[i].d_un.d_ptr;
			break;
		case DT_RELA:
			m->rela = m->bias + d[i].d_un.d_ptr;
			break;
		case DT_RELASZ:
			m->relasz = d[i].d_un.d_val;
			break;
		case DT_RELAENT:
			m->relaent = d[i].d_un.d_val;
			break;
		case DT_JMPREL:
			m->jmprel = m->bias + d[i].d_un.d_ptr;
			break;
		case DT_PLTRELSZ:
			m->pltrelsz = d[i].d_un.d_val;
			break;
		case DT_INIT:
			m->init = m->bias + d[i].d_un.d_ptr;
			break;
		case DT_INIT_ARRAY:
			m->init_array = m->bias + d[i].d_un.d_ptr;
			break;
		case DT_INIT_ARRAYSZ:
			m->init_arraysz = d[i].d_un.d_val;
			break;
		case DT_TEXTREL:
			ld_die("ld-brk: TEXTREL unsupported\n");
			break;
		}
	}

	for (i = 0; i < nneeded; ++i) {
		const char *nm =
			(const char *)m->strtab + (u32)needed[i]->d_un.d_val;

		if (!ld_mod_by_name(nm))
			ld_load_library(nm);
	}
}

static void ld_load_library(const char *name)
{
	static const char *const dirs[] = { "/lib/", "/usr/lib/" };
	char path[256];

	ld_syscall6(SYS_WRITE, 1, (long)"ld:load\n", 9, 0, 0, 0);
	struct ld_file f;
	struct ld_mod *m;
	ulong i;

	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
		ulong o = 0, j;

		for (j = 0; dirs[i][j]; ++j)
			path[o++] = dirs[i][j];
		for (j = 0; name[j] && o < sizeof(path) - 1; ++j)
			path[o++] = name[j];
		path[o] = 0;
		if (ld_slurp(path, &f) == 0) {
			ld_map_module(name, &f);
			m = ld_mods;
			while (m->next)
				m = m->next;
			ld_parse_dynamic(m);
			return;
		}
	}
	ld_write_str("ld-brk: cannot find library: ");
	ld_write_str(name);
	ld_write_str("\n");
	ld_die("ld-brk: DT_NEEDED unresolved\n");
}

/* ---- relocations: eager binding, PLT slots included ---- */

static void ld_reloc_range(struct ld_mod *m, ulong rela, ulong size,
			   ulong ent)
{
	struct __brk_elf_rela *r;
	ulong i;

	if (!rela || !size)
		return;
	if (ent != sizeof(*r) || size % ent)
		ld_die("ld-brk: bad relocation table\n");
	for (i = 0; i < size / ent; ++i) {
		u32 type, sym;
		ulong slot, val;
		const struct ld_sym *s;
		const char *nm;

		r = (struct __brk_elf_rela *)(rela + i * ent);
		type = (u32)(r->r_info & 0xff);
		sym = (u32)(r->r_info >> 32);
		slot = m->bias + r->r_offset;

		switch (type) {
		case R_RISCV_NONE:
		case R_RISCV_RELAX:
		case R_RISCV_ALIGN:
			continue;
		case R_RISCV_RELATIVE:
			*(ulong *)slot = m->bias + (ulong)r->r_addend;
			continue;
		case R_RISCV_64:
		case R_RISCV_GLOB_DAT:
		case R_RISCV_JUMP_SLOT:
			if (!m->symtab || sym == 0)
				ld_die("ld-brk: reloc without symbol\n");
			s = &((const struct ld_sym *)m->symtab)[sym];
			nm = (const char *)m->strtab + s->st_name;
			val = ld_lookup_global(nm);
			if (val == 0 && !(s->st_info & 0x20))
				ld_die("ld-brk: undefined symbol\n");
			*(ulong *)slot = val;
			continue;
		default:
			ld_die("ld-brk: unsupported relocation type\n");
		}
	}
}

static void ld_relocate(struct ld_mod *m)
{
	ld_reloc_range(m, m->rela, m->relasz, m->relaent);
	ld_reloc_range(m, m->jmprel, m->pltrelsz,
		      sizeof(struct __brk_elf_rela));
}

static void ld_run_inits(struct ld_mod *m)
{
	void (**arr)(void);
	ulong i;

	if (!m->init_array || !m->init_arraysz)
		goto plain_init;
	arr = (void (**)(void))m->init_array;
	for (i = 0; i < m->init_arraysz / sizeof(*arr); ++i)
		arr[i]();
plain_init:
	if (m->init)
		((void (*)(void))m->init)();
}

/* ---- startup ---- */

static ulong ld_auxv_find(ulong *auxv, u32 key)
{
	for (; auxv[0] != AT_NULL; auxv += 2)
		if ((u32)auxv[0] == key)
			return auxv[1];
	return 0;
}

static void __attribute__((noreturn))
transfer(ulong entry, long argc, char **argv, char **envp, ulong sp)
{
	/*
	 * Bind every input to a scratch or callee-saved register: the
	 * mv sequence writes a0..a2, so the compiler must never place
	 * an input there.
	 */
	register long rt0 asm("t0") = (long)entry;
	register long rt1 asm("t1") = argc;
	register long rt2 asm("t2") = (long)argv;
	register long rt3 asm("s2") = (long)envp;
	register long rt4 asm("s3") = (long)sp;

	asm volatile("mv sp, %4\n"
		     "mv a0, %1\n"
		     "mv a1, %2\n"
		     "mv a2, %3\n"
		     "jr %0\n"
		     :
		     : "r"(rt0), "r"(rt1), "r"(rt2), "r"(rt3), "r"(rt4)
		     : "memory");
	__builtin_unreachable();
}

void __attribute__((noreturn))
__brk_ldso_main(long argc, char **argv, char **envp, void *sp)
{
	ulong *auxv;
	ulong at_phdr, at_phent, at_phnum, at_entry;
	struct ld_mod *exe;
	struct __brk_elf_phdr *ph;
	ulong bias = 0;
	int i;

	ld_syscall6(SYS_WRITE, 1, (long)"ld:boot\n", 8, 0, 0, 0);

	for (auxv = (ulong *)envp; *auxv; ++auxv)
		;
	++auxv;
	ld_syscall6(SYS_WRITE, 1, (long)"ld:auxv\n", 9, 0, 0, 0);

	at_phdr = ld_auxv_find(auxv, AT_PHDR);
	at_phent = ld_auxv_find(auxv, AT_PHENT);
	at_phnum = ld_auxv_find(auxv, AT_PHNUM);
	at_entry = ld_auxv_find(auxv, AT_ENTRY);
	if (!at_phdr || !at_phnum || !at_entry ||
	    at_phent != sizeof(struct __brk_elf_phdr))
		ld_die("ld-brk: missing auxv\n");

	/*
	 * The main image's bias: PT_PHDR's link-time address versus
	 * where the kernel actually mapped the table.
	 */
	ph = (struct __brk_elf_phdr *)at_phdr;
	for (i = 0; i < (int)at_phnum; ++i)
		if (ph[i].p_type == PT_PHDR) {
			bias = at_phdr - ph[i].p_vaddr;
			break;
		}

	exe = ld_alloc(sizeof(*exe));
	ld_memset(exe, 0, sizeof(*exe));
	exe->name = "(exe)";
	exe->bias = bias;
	for (i = 0; i < (int)at_phnum; ++i)
		if (ph[i].p_type == PT_DYNAMIC) {
			exe->dyn = (struct __brk_elf_dyn *)(bias +
							    ph[i].p_vaddr);
			break;
		}
	if (!exe->dyn)
		ld_die("ld-brk: no PT_DYNAMIC in executable\n");
	ld_mods = exe;
	ld_syscall6(SYS_WRITE, 1, (long)"ld:exe\n", 8, 0, 0, 0);

	ld_parse_dynamic(exe);
	ld_syscall6(SYS_WRITE, 1, (long)"ld:needed\n", 11, 0, 0, 0);
	for (struct ld_mod *m = ld_mods; m; m = m->next)
		ld_relocate(m);
	for (struct ld_mod *m = ld_mods; m; m = m->next)
		ld_run_inits(m);

	ld_syscall6(SYS_WRITE, 1, (long)"ld:go\n", 7, 0, 0, 0);
	transfer(at_entry, argc, argv, envp, (ulong)sp);
}

/*
 * Entry: a0=argc a1=argv a2=envp per the BRK register convention,
 * sp still on the pristine SysV stack laid out by the kernel.
 */
asm(".globl _start\n"
    "_start:\n"
    "	mv a3, sp\n"
    "	call __brk_ldso_main\n");
