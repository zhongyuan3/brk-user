#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syscall.h>
#include <unistd.h>

static int case_malloc(struct test_ctx *ctx)
{
	unsigned char *p;
	unsigned char *q;

	(void)ctx;
	p = malloc(64 * 1024);
	if (!p)
		return test_fail_msg("memory: malloc failed");
	for (size_t i = 0; i < 64 * 1024; i++)
		p[i] = (unsigned char)(i * 31 + 7);

	q = realloc(p, 128 * 1024);
	if (!q)
		return test_fail_msg("memory: realloc failed");
	p = q;
	for (size_t i = 0; i < 64 * 1024; i++) {
		if (p[i] != (unsigned char)(i * 31 + 7))
			return test_fail_msg("memory: realloc lost data");
	}
	free(p);

	p = calloc(256, 128);
	if (!p)
		return test_fail_msg("memory: calloc failed");
	for (size_t i = 0; i < 256 * 128; i++) {
		if (p[i] != 0)
			return test_fail_msg("memory: calloc is not zeroed");
	}
	free(p);
	return 0;
}

static int case_mmap_anon(struct test_ctx *ctx)
{
	size_t len = 64 * 1024;
	unsigned char *p;

	(void)ctx;
	p = mmap(NULL, len, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return test_fail_msg("memory: anonymous mmap failed: %d", errno);

	for (size_t i = 0; i < len; i++)
		p[i] = (unsigned char)(i ^ 0x5a);
	for (size_t i = 0; i < len; i++) {
		if (p[i] != (unsigned char)(i ^ 0x5a))
			return test_fail_msg("memory: anonymous mmap data mismatch");
	}

	if (mprotect(p, len, PROT_READ) != 0)
		return test_fail_msg("memory: mprotect read-only failed");
	if (p[123] != (unsigned char)(123 ^ 0x5a))
		return test_fail_msg("memory: read after mprotect mismatch");
	if (mprotect(p, len, PROT_READ | PROT_WRITE) != 0)
		return test_fail_msg("memory: mprotect read-write failed");
	p[0] = 0xa5;
	if (munmap(p, len) != 0)
		return test_fail_msg("memory: munmap failed");
	return 0;
}

static int case_mmap_file(struct test_ctx *ctx)
{
	char dir[128], path[160];
	unsigned char *p;
	unsigned char buf[4096];
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "mmap") != 0)
		return test_fail_msg("memory: cannot create temp dir");
	int n = snprintf(path, sizeof(path), "%s/file", dir);
	if (n < 0 || (size_t)n >= sizeof(path))
		return -1;

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("memory: open failed: %d", errno);
	if (syscall(SYS_ftruncate, fd, 8192) != 0)
		return test_fail_msg("memory: ftruncate failed: %d", errno);

	p = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		return test_fail_msg("memory: file mmap failed: %d", errno);
	for (size_t i = 0; i < 8192; i++)
		p[i] = (unsigned char)(i * 7 + 3);
	if (msync(p, 8192, MS_SYNC) != 0)
		return test_fail_msg("memory: msync failed: %d", errno);
	if (munmap(p, 8192) != 0)
		return test_fail_msg("memory: munmap failed");

	if (lseek(fd, 0, SEEK_SET) != 0)
		return test_fail_msg("memory: lseek failed");
	if (read(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf))
		return test_fail_msg("memory: file read failed");
	for (size_t i = 0; i < sizeof(buf); i++) {
		if (buf[i] != (unsigned char)(i * 7 + 3))
			return test_fail_msg("memory: file mmap data mismatch");
	}
	close(fd);
	unlink(path);
	rmdir(dir);
	return 0;
}

static int case_fork_cow(struct test_ctx *ctx)
{
	unsigned char *p = malloc(4096);
	pid_t pid;
	int status;

	(void)ctx;
	if (!p)
		return test_fail_msg("memory: malloc failed");
	memset(p, 0x11, 4096);

	pid = fork();
	if (pid < 0)
		return test_fail_msg("memory: fork failed");
	if (pid == 0) {
		for (size_t i = 0; i < 4096; i++) {
			if (p[i] != 0x11)
				_exit(1);
		}
		memset(p, 0x22, 4096);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return test_fail_msg("memory: COW child failed");
	for (size_t i = 0; i < 4096; i++) {
		if (p[i] != 0x11)
			return test_fail_msg("memory: COW modified parent data");
	}
	free(p);
	return 0;
}

static const struct test_case memory_cases[] = {
	{ "malloc", case_malloc, TEST_OWN_PGRP, 10000 },
	{ "mmap_anon", case_mmap_anon, TEST_OWN_PGRP, 10000 },
	{ "mmap_file", case_mmap_file, TEST_OWN_PGRP, 10000 },
	{ "fork_cow", case_fork_cow, TEST_OWN_PGRP, 10000 },
};

const struct test_suite brktest_memory_suite = {
	.name = "memory",
	.description = "malloc, mmap, mprotect and copy-on-write",
	.cases = memory_cases,
	.nr_cases = sizeof(memory_cases) / sizeof(memory_cases[0]),
};
