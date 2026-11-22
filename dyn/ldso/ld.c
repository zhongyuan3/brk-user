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
#define LD_POOL_CAP (1UL << 16)
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
	ulong fini, fini_array, fini_arraysz;
	ulong lo, hi;			/* mapped extent (biased) */
	long refcount;			/* 0 = permanent, >0 = dl handles */
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

/* ---- file loading: segments are read straight into place ---- */

static long ld_seek_read(long fd, ulong off, void *buf, ulong n)
{
	long r;

	ld_syscall6(SYS_SEEK, fd, (long)off, 0 /* SEEK_SET */, 0, 0, 0);
	r = ld_syscall6(SYS_READ, fd, (long)buf, (long)n, 0, 0, 0);
	return (r >= 0 && (ulong)r == n) ? 0 : -1;
}

/*
 * Map a DSO from its file descriptor: anonymous MAP_FIXED pages at
 * a descending base, each PT_LOAD read straight into place with
 * SEEK+READ, non-writable segments sealed with mprotect.  The new
 * module lands at the tail of the load list.
 */
static void ld_map_module_fd(const char *name, long fd)
{
	struct __brk_elf_hdr hbuf;
	const struct __brk_elf_hdr *h = &hbuf;
	struct __brk_elf_phdr *ph;
	ulong link_base = (ulong)-1;
	ulong top = 0;
	struct ld_mod **tail;
	struct ld_mod *m;
	int i;

	if (ld_seek_read(fd, 0, &hbuf, sizeof(hbuf)))
		ld_die("ld-brk: short ELF\n");
	if (h->e_ident[0] != 0x7f || h->e_ident[1] != 'E' ||
	    h->e_ident[2] != 'L' || h->e_ident[3] != 'F' ||
	    h->e_ident[4] != ELFCLASS64 || h->e_machine != EM_RISCV ||
	    h->e_phentsize != sizeof(*ph) ||
	    h->e_phnum == 0 || h->e_phnum > 256)
		ld_die("ld-brk: bad ELF\n");

	ph = ld_alloc((ulong)h->e_phnum * sizeof(*ph));
	if (ld_seek_read(fd, h->e_phoff, ph,
			 (ulong)h->e_phnum * sizeof(*ph)))
		ld_die("ld-brk: short program headers\n");

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
	m->lo = m->bias + (link_base & ~(LD_PAGE - 1));
	m->hi = m->bias + ((top + LD_PAGE - 1) & ~(LD_PAGE - 1));

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
		/* beyond p_filesz the pages are fresh anonymous zero */
		if (ph[i].p_filesz &&
		    ld_seek_read(fd, ph[i].p_offset, (void *)vaddr,
				 ph[i].p_filesz))
			ld_die("ld-brk: short segment read\n");
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
		case DT_FINI:
			m->fini = m->bias + d[i].d_un.d_ptr;
			break;
		case DT_FINI_ARRAY:
			m->fini_array = m->bias + d[i].d_un.d_ptr;
			break;
		case DT_FINI_ARRAYSZ:
			m->fini_arraysz = d[i].d_un.d_val;
			break;
		case DT_SONAME:
			if (m->name == NULL)
				m->name = (const char *)m->strtab +
					  d[i].d_un.d_val;
			break;
		case DT_TEXTREL:
			ld_die("ld-brk: TEXTREL unsupported\n");
			break;
		}
	}

	for (i = 0; i < nneeded; ++i) {
		const char *nm =
			(const char *)m->strtab + (u32)needed[i]->d_un.d_val;
		struct ld_mod *dep = ld_mod_by_name(nm);

		if (dep) {
			dep->refcount++;
			continue;
		}
		ld_load_library(nm);
		dep = ld_mod_by_name(nm);
		if (dep)
			dep->refcount++;
	}
}

static void ld_load_library(const char *name)
{
	static const char *const dirs[] = { "/lib/", "/usr/lib/" };
	char path[256];
	struct ld_mod *m;
	ulong i;

	for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i) {
		ulong o = 0, j;
		long fd;

		for (j = 0; dirs[i][j] && o < sizeof(path) - 1; ++j)
			path[o++] = dirs[i][j];
		for (j = 0; name[j] && o < sizeof(path) - 1; ++j)
			path[o++] = name[j];
		path[o] = 0;
		fd = ld_open(path);
		if (fd < 0)
			continue;
		ld_map_module_fd(name, fd);
		ld_close((int)fd);
		m = ld_mods;
		while (m->next)
			m = m->next;
		ld_parse_dynamic(m);
		return;
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

/* ---- dlopen family ---- */

#define RTLD_LAZY 0x1
#define RTLD_NOW 0x2
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL 0
#define RTLD_DEFAULT ((void *)0)
#define RTLD_NEXT ((void *)-1L)

static char ld_dlerror_buf[128];
static const char *ld_dlerror_msg;

static void ld_set_dlerror(const char *msg)
{
	ulong i;

	for (i = 0; i + 1 < sizeof(ld_dlerror_buf) && msg[i]; ++i)
		ld_dlerror_buf[i] = msg[i];
	ld_dlerror_buf[i] = 0;
	ld_dlerror_msg = ld_dlerror_buf;
}

/* LD_LIBRARY_PATH captured at startup */
static const char *ld_env_libpath;

static char *ld_strdup(const char *s)
{
	ulong n = ld_strlen(s) + 1;
	char *p = ld_alloc(n);

	ld_memcpy(p, s, n);
	return p;
}

/*
 * The linker registers itself as a module so its exported dl* API
 * is visible in the global symbol scope; the kernel already applied
 * its relocations and the image is fully live.
 */
static void ld_register_self(ulong at_base)
{
	const struct __brk_elf_hdr *h = (const struct __brk_elf_hdr *)at_base;
	const struct __brk_elf_phdr *ph;
	struct ld_mod *m;
	ulong link_base = (ulong)-1;
	ulong top = 0;
	ulong dyn_vaddr = 0;
	int i;

	if (h->e_ident[0] != 0x7f || h->e_ident[1] != 'E' ||
	    h->e_phentsize != sizeof(*ph))
		return;
	ph = (const struct __brk_elf_phdr *)(at_base + h->e_phoff);
	for (i = 0; i < h->e_phnum; ++i) {
		if (ph[i].p_type == PT_LOAD) {
			if (ph[i].p_vaddr < link_base)
				link_base = ph[i].p_vaddr;
			if (ph[i].p_vaddr + ph[i].p_memsz > top)
				top = ph[i].p_vaddr + ph[i].p_memsz;
		}
		if (ph[i].p_type == PT_DYNAMIC)
			dyn_vaddr = ph[i].p_vaddr;
	}
	if (link_base == (ulong)-1 || !dyn_vaddr)
		return;

	m = ld_alloc(sizeof(*m));
	ld_memset(m, 0, sizeof(*m));
	m->name = "ld-brk.so";
	m->bias = at_base - link_base;
	m->dyn = (struct __brk_elf_dyn *)(m->bias + dyn_vaddr);
	m->lo = at_base;
	m->hi = m->bias + ((top + LD_PAGE - 1) & ~(LD_PAGE - 1));
	ld_parse_dynamic(m);
	{
		struct ld_mod **tail = &ld_mods;

		while (*tail)
			tail = &(*tail)->next;
		*tail = m;
	}
}

static void ld_unmap_module(struct ld_mod *m)
{
	const struct __brk_elf_hdr *h = (const struct __brk_elf_hdr *)
		(m->lo - m->bias > 0 ? (m->bias ? (void *)0 : (void *)0) : 0);
	(void)h;
	/*
	 * The extent is page-aligned on both ends, so one munmap per
	 * module suffices; the kernel splits it internally.
	 */
	ld_syscall6(SYS_MUNMAP, (long)m->lo, (long)(m->hi - m->lo), 0, 0, 0,
		    0);
}

static void ld_run_fini(struct ld_mod *m)
{
	void (**arr)(void);
	ulong i;

	if (m->fini_array && m->fini_arraysz) {
		arr = (void (**)(void))m->fini_array;
		for (i = m->fini_arraysz / sizeof(*arr); i > 0; --i)
			arr[i - 1]();
	}
	if (m->fini)
		((void (*)(void))m->fini)();
}

static void ld_dlclose_locked(struct ld_mod *m)
{
	struct __brk_elf_dyn *d = m->dyn;
	const char *deps[LD_MAX_MODS];
	int ndeps = 0;
	int i;

	/* collect dependencies before the dynamic table disappears */
	for (i = 0; d && d[i].d_tag != DT_NULL; ++i)
		if ((u32)d[i].d_tag == DT_NEEDED && ndeps < LD_MAX_MODS)
			deps[ndeps++] =
				(const char *)m->strtab + (u32)d[i].d_un.d_val;

	ld_run_fini(m);

	{
		struct ld_mod **pp = &ld_mods;

		while (*pp && *pp != m)
			pp = &(*pp)->next;
		if (*pp)
			*pp = m->next;
	}
	ld_unmap_module(m);

	for (i = 0; i < ndeps; ++i) {
		struct ld_mod *dep = ld_mod_by_name(deps[i]);

		if (dep && dep->refcount > 0 && --dep->refcount == 0)
			ld_dlclose_locked(dep);
	}
}

static struct ld_mod *ld_open_module(const char *name, const char *path)
{
	long fd;
	struct ld_mod *m;

	fd = ld_open(path);
	if (fd < 0)
		return NULL;
	ld_map_module_fd(name, fd);
	ld_close((int)fd);
	m = ld_mod_by_name(name);
	if (!m)
		return NULL;
	m->refcount = 1;
	ld_parse_dynamic(m);
	ld_relocate(m);
	ld_run_inits(m);
	return m;
}

__attribute__((visibility("default")))
void *dlopen(const char *name, int flags)
{
	static const char *const dirs[] = { "/lib/", "/usr/lib/" };
	struct ld_mod *m;
	const char *base = name;
	char path[256];
	ulong i;

	if (flags & ~(RTLD_LAZY | RTLD_NOW | RTLD_GLOBAL | RTLD_LOCAL)) {
		ld_set_dlerror("dlopen: unsupported flags");
		return NULL;
	}
	if (!name) {
		/* NULL opens the calling program itself */
		ld_dlerror_msg = NULL;
		return ld_mods;
	}

	/* identity and reporting use the basename */
	for (i = 0; name[i]; ++i)
		if (name[i] == '/')
			base = name + i + 1;

	m = ld_mod_by_name(base);
	if (m) {
		if (m->refcount == 0)
			m->refcount = 1;
		else
			m->refcount++;
		ld_dlerror_msg = NULL;
		return m;
	}

	/* a name with a slash is a path: open it directly */
	for (i = 0; name[i]; ++i) {
		if (name[i] != '/')
			continue;
		m = ld_open_module(base, name);
		if (m) {
			ld_dlerror_msg = NULL;
			return m;
		}
		goto notfound;
	}

	/* bare names search LD_LIBRARY_PATH then the default dirs */
	{
		const char *cand[16];
		int nc = 0;

		if (ld_env_libpath)
			for (i = 0; ld_env_libpath[i] && nc < 14;) {
				ulong o = 0;

				while (ld_env_libpath[i] == ':')
					++i;
				while (ld_env_libpath[i] &&
				       ld_env_libpath[i] != ':' &&
				       o < sizeof(path) - 1)
					path[o++] = ld_env_libpath[i++];
				if (o) {
					if (path[o - 1] != '/')
						path[o++] = '/';
					path[o] = 0;
					cand[nc++] = ld_strdup(path);
				}
			}
		for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); ++i)
			cand[nc++] = dirs[i];

		for (i = 0; i < (ulong)nc; ++i) {
			ulong o = 0, j;

			for (j = 0; cand[i][j] && o < sizeof(path) - 1; ++j)
				path[o++] = cand[i][j];
			for (j = 0; base[j] && o < sizeof(path) - 1; ++j)
				path[o++] = base[j];
			path[o] = 0;
			m = ld_open_module(base, path);
			if (m) {
				ld_dlerror_msg = NULL;
				return m;
			}
		}
	}

notfound:
	ld_set_dlerror("dlopen: cannot open shared object");
	return NULL;
}

__attribute__((visibility("default")))
void *dlsym(void *handle, const char *name)
{
	struct ld_mod *m;

	if (handle == RTLD_DEFAULT || handle == NULL) {
		ulong a = ld_lookup_global(name);

		if (a) {
			ld_dlerror_msg = NULL;
			return (void *)a;
		}
		ld_set_dlerror("dlsym: undefined symbol");
		return NULL;
	}
	if (handle == RTLD_NEXT) {
		/*
		 * Continue the search after the caller's module: find
		 * which mapped module contains the return address.
		 */
		ulong ra = (ulong)__builtin_return_address(0);
		struct ld_mod *c;

		for (c = ld_mods; c; c = c->next)
			if (ra >= c->lo && ra < c->hi)
				break;
		for (c = c ? c->next : NULL; c; c = c->next)
			if (ld_lookup_local(c, name)) {
				ld_dlerror_msg = NULL;
				return (void *)ld_lookup_local(c, name);
			}
		ld_set_dlerror("dlsym: RTLD_NEXT: symbol not found");
		return NULL;
	}

	for (m = ld_mods; m; m = m->next)
		if (m == (struct ld_mod *)handle)
			break;
	if (!m) {
		ld_set_dlerror("dlsym: invalid handle");
		return NULL;
	}
	{
		ulong a = ld_lookup_local(m, name);

		if (!a)
			a = ld_lookup_global(name);
		if (a) {
			ld_dlerror_msg = NULL;
			return (void *)a;
		}
		ld_set_dlerror("dlsym: undefined symbol");
		return NULL;
	}
}

__attribute__((visibility("default")))
int dlclose(void *handle)
{
	struct ld_mod *m;

	if (handle == RTLD_DEFAULT || handle == RTLD_NEXT ||
	    handle == NULL) {
		ld_set_dlerror("dlclose: invalid handle");
		return -1;
	}
	for (m = ld_mods; m; m = m->next)
		if (m == (struct ld_mod *)handle)
			break;
	if (!m) {
		ld_set_dlerror("dlclose: invalid handle");
		return -1;
	}
	if (m->refcount == 0) {
		/* startup module: permanent, closing is a no-op */
		ld_dlerror_msg = NULL;
		return 0;
	}
	if (--m->refcount > 0) {
		ld_dlerror_msg = NULL;
		return 0;
	}
	ld_dlclose_locked(m);
	ld_dlerror_msg = NULL;
	return 0;
}

__attribute__((visibility("default")))
char *dlerror(void)
{
	const char *msg = ld_dlerror_msg;

	ld_dlerror_msg = NULL;
	return (char *)msg;
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

	{
		char **e;

		for (e = envp; *e; ++e) {
			const char *k = "LD_LIBRARY_PATH=";

			for (i = 0; k[i] && (*e)[i] == k[i]; ++i)
				;
			if (!k[i]) {
				ld_env_libpath = *e + i;
				break;
			}
		}
	}

	for (auxv = (ulong *)envp; *auxv; ++auxv)
		;
	++auxv;

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

	ld_register_self(ld_auxv_find(auxv, AT_BASE));
	ld_parse_dynamic(exe);
	for (struct ld_mod *m = ld_mods; m; m = m->next)
		ld_relocate(m);
	for (struct ld_mod *m = ld_mods; m; m = m->next)
		ld_run_inits(m);

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
