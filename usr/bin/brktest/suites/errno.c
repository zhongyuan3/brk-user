#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>

static int case_badfd(struct test_ctx *ctx)
{
	char dir[128], path[160];
	char buf[4];
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "badfd") != 0)
		return test_fail_msg("errno: cannot create temp dir");
	if (snprintf(path, sizeof(path), "%s/file", dir) < 0)
		return -1;
	errno = 0;
	if (read(-1, buf, sizeof(buf)) != -1 || errno != EBADF)
		return test_fail_msg("errno: read(-1) did not return EBADF");
	errno = 0;
	if (write(-1, buf, 1) != -1 || errno != EBADF)
		return test_fail_msg("errno: write(-1) did not return EBADF");
	errno = 0;
	if (close(-1) != -1 || errno != EBADF)
		return test_fail_msg("errno: close(-1) did not return EBADF");
	errno = 0;
	if (lseek(-1, 0, SEEK_SET) != -1 || errno != EBADF)
		return test_fail_msg("errno: lseek(-1) did not return EBADF");
	errno = 0;
	if (dup(-1) != -1 || errno != EBADF)
		return test_fail_msg("errno: dup(-1) did not return EBADF");
	errno = 0;
	if (waitpid(0, NULL, 0) != -1 || errno != EINVAL)
		return test_fail_msg("errno: waitpid(0) did not return EINVAL");

	fd = open(path, O_RDONLY | O_CREAT, 0644);
	if (fd < 0)
		return test_fail_msg("errno: open test file failed: %d", errno);
	errno = 0;
	if (close(fd) != 0)
		return test_fail_msg("errno: close succeeded fd failed");
	errno = 0;
	if (close(fd) != -1 || errno != EBADF)
		return test_fail_msg("errno: double close did not return EBADF");
	unlink(path);
	rmdir(dir);
	return 0;
}

static int case_paths(struct test_ctx *ctx)
{
	char dir[128], file[160], missing[160];
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "errno") != 0)
		return test_fail_msg("errno: cannot create temp dir");
	if (snprintf(file, sizeof(file), "%s/file", dir) < 0 ||
	    snprintf(missing, sizeof(missing), "%s/missing", dir) < 0)
		return -1;

	errno = 0;
	if (open(missing, O_RDONLY) != -1 || errno != ENOENT)
		return test_fail_msg("errno: open missing did not return ENOENT");
	struct stat st;

	errno = 0;
	if (stat(missing, &st) != -1 || errno != ENOENT)
		return test_fail_msg("errno: stat missing did not return ENOENT");
	errno = 0;
	if (unlink(missing) != -1 || errno != ENOENT)
		return test_fail_msg("errno: unlink missing did not return ENOENT");
	errno = 0;
	if (mkdir(dir, 0755) != -1 || errno != EEXIST)
		return test_fail_msg("errno: mkdir existing did not return EEXIST");

	fd = open(file, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0)
		return test_fail_msg("errno: create test file failed: %d", errno);
	close(fd);
	errno = 0;
	if (open(file, O_WRONLY | O_CREAT | O_EXCL, 0644) != -1 ||
	    errno != EEXIST)
		return test_fail_msg("errno: O_EXCL did not return EEXIST");
	fd = open(dir, O_RDONLY);
	if (fd < 0)
		return test_fail_msg("errno: open directory failed: %d", errno);
	errno = 0;
	if (write(fd, "x", 1) != -1 || errno != EISDIR)
		return test_fail_msg("errno: write directory did not return EISDIR");
	close(fd);
	errno = 0;
	if (rmdir(file) != -1 || errno != ENOTDIR)
		return test_fail_msg("errno: rmdir file did not return ENOTDIR");
	fd = open(file, O_WRONLY);
	if (fd < 0)
		return test_fail_msg("errno: reopen failed: %d", errno);
	errno = 0;
	if (ftruncate(fd, -1) != -1 || errno != EINVAL)
		return test_fail_msg("errno: ftruncate negative length was accepted");
	close(fd);

	unlink(file);
	rmdir(dir);
	return 0;
}

static int case_null(struct test_ctx *ctx)
{
	(void)ctx;
	errno = 0;
	if (uname(NULL) != -1 || errno != EFAULT)
		return test_fail_msg("errno: uname(NULL) did not return EFAULT");
	return 0;
}

static int case_flags(struct test_ctx *ctx)
{
	int pipefd[2];
	char dir[128], file[160];

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "flags") != 0)
		return test_fail_msg("errno: cannot create temp dir");
	if (snprintf(file, sizeof(file), "%s/file", dir) < 0)
		return -1;
	errno = 0;
	if (pipe2(pipefd, 0x40000000) != -1 || errno != EINVAL)
		return test_fail_msg("errno: pipe2 bad flags did not return EINVAL");
	errno = 0;
	if (truncate(file, 0) != -1 || errno != ENOENT)
		return test_fail_msg("errno: truncate missing did not return ENOENT");
	errno = 0;
	if (fchmod(-1, 0644) != -1 || errno != EBADF)
		return test_fail_msg("errno: fchmod bad fd did not return EBADF");
	rmdir(dir);
	return 0;
}

static const struct test_case errno_cases[] = {
	{ "badfd", case_badfd, 0, 10000 },
	{ "paths", case_paths, TEST_OWN_PGRP, 10000 },
	{ "null", case_null, 0, 10000 },
	{ "flags", case_flags, TEST_OWN_PGRP, 10000 },
};

const struct test_suite brktest_errno_suite = {
	.name = "errno",
	.description = "error paths, invalid arguments and user pointers",
	.cases = errno_cases,
	.nr_cases = sizeof(errno_cases) / sizeof(errno_cases[0]),
};
