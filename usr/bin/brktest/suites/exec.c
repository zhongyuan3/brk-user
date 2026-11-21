/*
 * ELF loader suite: the kernel exec path is probed with images crafted
 * byte by byte.  The reject cases pin the validation policy (header
 * fields, dynamic-linking phdrs, overlapping segments, entry), the
 * accept cases prove that a minimal ET_EXEC, a GNU_STACK executable
 * and a static PIE with kernel-applied R_RISCV_RELATIVE relocations
 * all actually run.
 */
#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <uapi/auxv.h>
#include <uapi/elf.h>
#include <unistd.h>

#define IMG_BUF_SZ 0x3000
#define CODE_OFF 0x1000
#define PIE_CODE_OFF 0x2000

/* exit(42) via the raw ABI: li a7,SYS_EXIT(41); li a0,42; ecall */
static const unsigned char exit42_code[] = {
	0x93, 0x08, 0x90, 0x02,
	0x13, 0x05, 0xa0, 0x02,
	0x73, 0x00, 0x00, 0x00,
};

/*
 * Base-relative probe: auipc a5,0          a5 = runtime pc
 *   ld a6,0x60(a5)                        a6 = slot = base + addend
 *   li a7,41                              exit syscall number
 *   lui a4,0x20                           a4 = link-time 0x20000
 *   sub a0,a6,a5  ; add a0,a0,a4          a0 = addend (base cancels)
 *   ecall
 * The exit status proves the kernel applied the relocation at an
 * unknown random PIE base.
 */
static const unsigned char pie_probe_code[] = {
	0x97, 0x07, 0x00, 0x00,
	0x03, 0x88, 0x07, 0x06,
	0x93, 0x08, 0x90, 0x02,
	0x37, 0x07, 0x02, 0x00,
	0x33, 0x05, 0xf8, 0x40,
	0x33, 0x05, 0xe5, 0x00,
	0x73, 0x00, 0x00, 0x00,
};

static void put16(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, uint32_t v)
{
	put16(p, (unsigned)v);
	put16(p + 2, (unsigned)(v >> 16));
}

static void put64(unsigned char *p, uint64_t v)
{
	put32(p, (uint32_t)v);
	put32(p + 4, (uint32_t)(v >> 32));
}

struct img {
	unsigned char buf[IMG_BUF_SZ];
};

static void img_ehdr(struct img *im, uint16_t type, uint64_t entry,
		     uint16_t phnum, uint32_t flags)
{
	memset(im->buf, 0, IMG_BUF_SZ);
	memcpy(im->buf, "\177ELF", 4);
	im->buf[4] = ELFCLASS64;
	im->buf[5] = ELFDATA2LSB;
	im->buf[6] = EV_CURRENT;
	put16(im->buf + 16, type);
	put16(im->buf + 18, EM_RISCV);
	put32(im->buf + 20, EV_CURRENT);
	put64(im->buf + 24, entry);
	put64(im->buf + 32, 64);		/* e_phoff */
	put32(im->buf + 48, flags);
	put16(im->buf + 52, 64);		/* e_ehsize */
	put16(im->buf + 54, 56);		/* e_phentsize */
	put16(im->buf + 56, phnum);
}

static void img_phdr(struct img *im, int idx, uint32_t type,
		     uint32_t flags, uint64_t off, uint64_t vaddr,
		     uint64_t filesz, uint64_t memsz, uint64_t align)
{
	unsigned char *p = im->buf + 64 + (size_t)idx * 56;

	put32(p, type);
	put32(p + 4, flags);
	put64(p + 8, off);
	put64(p + 16, vaddr);
	put64(p + 24, vaddr);			/* p_paddr */
	put64(p + 32, filesz);
	put64(p + 40, memsz);
	put64(p + 48, align);
}

static int img_write(const struct img *im, size_t len, const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);

	if (fd < 0)
		return -1;
	if (write(fd, im->buf, len) != (ssize_t)len) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

/*
 * Child exit codes: 1 = execve refused with ENOEXEC, 2 = execve failed
 * with a different errno, 3 = execve unexpectedly returned success.
 * Accept cases pass through into the crafted code instead.
 */
static int run_image(const char *path)
{
	char *argv[2] = { (char *)path, NULL };
	char *envp[1] = { NULL };
	pid_t pid = fork();
	int status;

	if (pid < 0)
		return -1;
	if (pid == 0) {
		execve(path, argv, envp);
		_exit(errno == ENOEXEC ? 1 : 2);
	}
	if (waitpid(pid, &status, 0) != pid)
		return -1;
	return status;
}

static int exec_ok_status(void)
{
	int status = run_image("/tmp/brktest-exec");

	if (status < 0)
		return test_fail_msg("exec: fork/wait failed");
	if (status != (42 << 8))
		return test_fail_msg("exec: wrong child status %#x", status);
	return 0;
}

static int exec_refused(void)
{
	int status = run_image("/tmp/brktest-exec");

	if (status < 0)
		return test_fail_msg("exec: fork/wait failed");
	if (status != (1 << 8))
		return test_fail_msg("exec: not refused: status %#x", status);
	return 0;
}

static int case_static_exec(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x10000, 1,
		 EF_RISCV_FLOAT_ABI_DOUBLE);
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_X, CODE_OFF, 0x10000,
		 sizeof(exit42_code), sizeof(exit42_code), 0x1000);
	memcpy(im.buf + CODE_OFF, exit42_code, sizeof(exit42_code));
	if (img_write(&im, CODE_OFF + sizeof(exit42_code),
		      "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_ok_status();
}

static int case_gnu_stack_x(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x10000, 2,
		 EF_RISCV_FLOAT_ABI_DOUBLE);
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_X, CODE_OFF, 0x10000,
		 sizeof(exit42_code), sizeof(exit42_code), 0x1000);
	img_phdr(&im, 1, PT_GNU_STACK, PF_R | PF_W | PF_X, 0, 0, 0, 0, 0);
	memcpy(im.buf + CODE_OFF, exit42_code, sizeof(exit42_code));
	if (img_write(&im, CODE_OFF + sizeof(exit42_code),
		      "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_ok_status();
}

static int case_pie_rela(struct test_ctx *ctx)
{
	struct img im;
	unsigned char *dyn, *rela;

	(void)ctx;
	img_ehdr(&im, ET_DYN, 0x20000, 2,
		 EF_RISCV_FLOAT_ABI_DOUBLE);
	/* one RWX load: code, slot at +0x60, dyn at +0x80, rela at +0xc0 */
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_W | PF_X, PIE_CODE_OFF,
		 0x20000, 0xe0, 0xe0, 0x1000);
	img_phdr(&im, 1, PT_DYNAMIC, PF_R | PF_W, PIE_CODE_OFF + 0x80,
		 0x20080, 0x40, 0x40, 8);
	memcpy(im.buf + PIE_CODE_OFF, pie_probe_code,
	       sizeof(pie_probe_code));

	dyn = im.buf + PIE_CODE_OFF + 0x80;
	put64(dyn, DT_RELA);
	put64(dyn + 8, 0x200c0);
	put64(dyn + 16, DT_RELASZ);
	put64(dyn + 24, 24);
	put64(dyn + 32, DT_RELAENT);
	put64(dyn + 40, 24);
	put64(dyn + 48, DT_NULL);
	put64(dyn + 56, 0);

	rela = im.buf + PIE_CODE_OFF + 0xc0;
	put64(rela, 0x20060);		/* the slot at +0x60 */
	put64(rela + 8, R_RISCV_RELATIVE);
	put64(rela + 16, 42);

	if (img_write(&im, PIE_CODE_OFF + 0xe0, "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_ok_status();
}

static int case_dynamic(struct test_ctx *ctx)
{
	struct stat st;

	(void)ctx;
	/*
	 * The full dynamic chain: PT_INTERP=/lib/ld-brk.so maps the
	 * linker, it loads DT_NEEDED libhello.so, resolves the weak
	 * symbols, runs the constructor and hands control to the
	 * entry.  dynhello exits 0 after printing "dyn ok".
	 */
	if (stat("/home/tests/libc/dynhello", &st) != 0)
		return test_skip("exec: dynhello not installed");
	int status = run_image("/home/tests/libc/dynhello");

	if (status < 0)
		return test_fail_msg("exec: fork/wait failed");
	if (status != 0)
		return test_fail_msg("exec: dynhello status %#x", status);
	return 0;
}

static int case_bad_magic(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x10000, 1, EF_RISCV_FLOAT_ABI_DOUBLE);
	im.buf[0] = 0x7e;
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_X, CODE_OFF, 0x10000,
		 sizeof(exit42_code), sizeof(exit42_code), 0x1000);
	if (img_write(&im, CODE_OFF + 16, "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_refused();
}

static int case_bad_class(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x10000, 1, EF_RISCV_FLOAT_ABI_DOUBLE);
	im.buf[4] = 1;			/* ELFCLASS32 */
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_X, CODE_OFF, 0x10000,
		 sizeof(exit42_code), sizeof(exit42_code), 0x1000);
	if (img_write(&im, CODE_OFF + 16, "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_refused();
}

static int case_bad_machine(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x10000, 1, EF_RISCV_FLOAT_ABI_DOUBLE);
	put16(im.buf + 18, EM_RISCV + 1);
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_X, CODE_OFF, 0x10000,
		 sizeof(exit42_code), sizeof(exit42_code), 0x1000);
	if (img_write(&im, CODE_OFF + 16, "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_refused();
}

static int case_bad_phentsize(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x10000, 1, EF_RISCV_FLOAT_ABI_DOUBLE);
	put16(im.buf + 54, 32);
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_X, CODE_OFF, 0x10000,
		 sizeof(exit42_code), sizeof(exit42_code), 0x1000);
	if (img_write(&im, CODE_OFF + 16, "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_refused();
}

static int case_soft_float(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x10000, 1, EF_RISCV_FLOAT_ABI_SOFT);
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_X, CODE_OFF, 0x10000,
		 sizeof(exit42_code), sizeof(exit42_code), 0x1000);
	if (img_write(&im, CODE_OFF + 16, "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_refused();
}

static int case_has_interp(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x10000, 2, EF_RISCV_FLOAT_ABI_DOUBLE);
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_X, CODE_OFF, 0x10000,
		 sizeof(exit42_code), sizeof(exit42_code), 0x1000);
	img_phdr(&im, 1, PT_INTERP, PF_R, CODE_OFF + 16, 0x20000, 4, 4, 1);
	memcpy(im.buf + CODE_OFF, exit42_code, sizeof(exit42_code));
	memcpy(im.buf + CODE_OFF + 16, "/li", 4);
	if (img_write(&im, CODE_OFF + 20, "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	/* must be refused, never silently executed bare */
	return exec_refused();
}

static int case_overlap(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x10000, 2, EF_RISCV_FLOAT_ABI_DOUBLE);
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_X, CODE_OFF, 0x10000, 0x2000,
		 0x2000, 0x1000);
	img_phdr(&im, 1, PT_LOAD, PF_R | PF_W, CODE_OFF + 0x2000, 0x11000,
		 0x100, 0x100, 0x1000);
	if (img_write(&im, CODE_OFF + 0x2100, "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_refused();
}

static int case_entry_not_x(struct test_ctx *ctx)
{
	struct img im;

	(void)ctx;
	img_ehdr(&im, ET_EXEC, 0x11000, 1, EF_RISCV_FLOAT_ABI_DOUBLE);
	/* single writable load; the entry is mapped but not executable */
	img_phdr(&im, 0, PT_LOAD, PF_R | PF_W, CODE_OFF, 0x10000, 0x2000,
		 0x2000, 0x1000);
	memcpy(im.buf + CODE_OFF + 0x1000, exit42_code,
	       sizeof(exit42_code));
	if (img_write(&im, CODE_OFF + 0x1000 + sizeof(exit42_code),
		      "/tmp/brktest-exec"))
		return test_fail_msg("exec: cannot write image");
	return exec_refused();
}

static const struct test_case exec_cases[] = {
	{ "dynamic", case_dynamic, 0, 10000 },
	{ "static_exec", case_static_exec, 0, 10000 },
	{ "pie_rela", case_pie_rela, 0, 10000 },
	{ "gnu_stack_x", case_gnu_stack_x, 0, 10000 },
	{ "bad_magic", case_bad_magic, 0, 10000 },
	{ "bad_class", case_bad_class, 0, 10000 },
	{ "bad_machine", case_bad_machine, 0, 10000 },
	{ "bad_phentsize", case_bad_phentsize, 0, 10000 },
	{ "soft_float", case_soft_float, 0, 10000 },
	{ "has_interp", case_has_interp, 0, 10000 },
	{ "overlap", case_overlap, 0, 10000 },
	{ "entry_not_x", case_entry_not_x, 0, 10000 },
};

const struct test_suite brktest_exec_suite = {
	.name = "exec",
	.description = "ELF loader validation and static PIE support",
	.cases = exec_cases,
	.nr_cases = sizeof(exec_cases) / sizeof(exec_cases[0]),
};
