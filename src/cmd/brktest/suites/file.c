#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syscall.h>
#include <unistd.h>

static int make_path(char *path, size_t size, const char *dir,
		     const char *name)
{
	int n = snprintf(path, size, "%s/%s", dir, name);

	return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

static int case_basic(struct test_ctx *ctx)
{
	char dir[128], path[160];
	char buf[32];
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "file") != 0)
		return test_fail_msg("file: cannot create temp dir");
	if (make_path(path, sizeof(path), dir, "basic") != 0)
		return test_fail_msg("file: path too long");

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("file: open failed: %d", errno);
	if (write(fd, "hello world", 11) != 11)
		return test_fail_msg("file: write failed: %d", errno);
	if (lseek(fd, 0, SEEK_SET) != 0)
		return test_fail_msg("file: lseek SET failed");
	if (read(fd, buf, sizeof(buf)) != 11 ||
	    memcmp(buf, "hello world", 11))
		return test_fail_msg("file: read back mismatch");
	if (lseek(fd, 0, SEEK_END) != 11)
		return test_fail_msg("file: lseek END failed");
	if (lseek(fd, -5, SEEK_CUR) != 6)
		return test_fail_msg("file: lseek CUR failed");
	if (read(fd, buf, sizeof(buf)) != 5 || memcmp(buf, "world", 5))
		return test_fail_msg("file: partial read mismatch");
	close(fd);
	unlink(path);
	rmdir(dir);
	return 0;
}

static int case_append(struct test_ctx *ctx)
{
	char dir[128], path[160];
	char buf[8] = { 0 };
	struct stat st;
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "append") != 0)
		return test_fail_msg("append: cannot create temp dir");
	if (make_path(path, sizeof(path), dir, "file") != 0)
		return -1;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
	if (fd < 0)
		return test_fail_msg("append: open failed: %d", errno);
	if (write(fd, "a", 1) != 1 || write(fd, "b", 1) != 1)
		return test_fail_msg("append: write failed: %d", errno);
	if (fstat(fd, &st) != 0 || st.st_size != 2)
		return test_fail_msg("append: size is not 2");
	close(fd);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	if (read(fd, buf, sizeof(buf)) != 2 || memcmp(buf, "ab", 2))
		return test_fail_msg("append: content mismatch");
	close(fd);
	unlink(path);
	rmdir(dir);
	return 0;
}

static int case_truncate(struct test_ctx *ctx)
{
	char dir[128], path[160];
	struct stat st;
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "truncate") != 0)
		return test_fail_msg("truncate: cannot create temp dir");
	if (make_path(path, sizeof(path), dir, "file") != 0)
		return -1;

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	if (write(fd, "01234567", 8) != 8)
		return test_fail_msg("truncate: write failed");
	if (syscall(SYS_ftruncate, fd, 4) != 0)
		return test_fail_msg("truncate: ftruncate failed: %d", errno);
	if (fstat(fd, &st) != 0 || st.st_size != 4)
		return test_fail_msg("truncate: size after ftruncate is %ld",
				     (long)st.st_size);
	close(fd);
	if (syscall(SYS_truncate, path, 2) != 0)
		return test_fail_msg("truncate: truncate failed: %d", errno);
	if (stat(path, &st) != 0 || st.st_size != 2)
		return test_fail_msg("truncate: size after truncate is %ld",
				     (long)st.st_size);
	unlink(path);
	rmdir(dir);
	return 0;
}

static int case_open_flags(struct test_ctx *ctx)
{
	char dir[128], path[160];
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "openflags") != 0)
		return test_fail_msg("openflags: cannot create temp dir");
	if (make_path(path, sizeof(path), dir, "file") != 0)
		return -1;

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0)
		return test_fail_msg("openflags: first O_EXCL failed: %d", errno);
	close(fd);
	if (open(path, O_WRONLY | O_CREAT | O_EXCL, 0644) >= 0)
		return test_fail_msg("openflags: second O_EXCL succeeded");
	if (errno != EEXIST)
		return test_fail_msg("openflags: O_EXCL errno is %d", errno);
	if (open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644) < 0)
		return test_fail_msg("openflags: O_TRUNC failed");
	unlink(path);
	rmdir(dir);
	return 0;
}

static int case_rename(struct test_ctx *ctx)
{
	char dir[128], oldpath[160], newpath[160];
	char buf[8] = { 0 };
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "rename") != 0)
		return test_fail_msg("rename: cannot create temp dir");
	if (make_path(oldpath, sizeof(oldpath), dir, "old") != 0 ||
	    make_path(newpath, sizeof(newpath), dir, "new") != 0)
		return -1;

	fd = open(oldpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	if (write(fd, "data", 4) != 4)
		return test_fail_msg("rename: write failed");
	close(fd);

	if (rename(oldpath, newpath) != 0)
		return test_fail_msg("rename: rename failed: %d", errno);
	if (open(oldpath, O_RDONLY) >= 0 || errno != ENOENT)
		return test_fail_msg("rename: old path still exists");
	fd = open(newpath, O_RDONLY);
	if (fd < 0)
		return test_fail_msg("rename: new path missing");
	if (read(fd, buf, sizeof(buf)) != 4 || memcmp(buf, "data", 4))
		return test_fail_msg("rename: content mismatch");
	close(fd);
	unlink(newpath);
	rmdir(dir);
	return 0;
}

static int case_dir_ops(struct test_ctx *ctx)
{
	char dir[128], subdir[160], path[192];
	struct stat st;
	int dfd, fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "dir") != 0)
		return test_fail_msg("dir: cannot create temp dir");
	if (make_path(subdir, sizeof(subdir), dir, "sub") != 0)
		return -1;
	if (mkdir(subdir, 0755) != 0)
		return test_fail_msg("dir: mkdir failed: %d", errno);
	if (stat(subdir, &st) != 0 || !S_ISDIR(st.st_mode))
		return test_fail_msg("dir: stat is not a directory");

	dfd = open(subdir, O_RDONLY | O_DIRECTORY);
	if (dfd < 0)
		return test_fail_msg("dir: O_DIRECTORY open failed: %d", errno);
	fd = openat(dfd, "file", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("dir: openat create failed: %d", errno);
	if (write(fd, "x", 1) != 1)
		return test_fail_msg("dir: openat write failed");
	close(fd);
	if (fstatat(dfd, "file", &st, 0) != 0 || st.st_size != 1)
		return test_fail_msg("dir: fstatat failed");
	if (unlinkat(dfd, "file", 0) != 0)
		return test_fail_msg("dir: unlinkat failed: %d", errno);
	if (fstatat(dfd, "file", &st, 0) == 0 || errno != ENOENT)
		return test_fail_msg("dir: unlinked file still visible");
	close(dfd);
	rmdir(subdir);
	rmdir(dir);
	(void)path;
	return 0;
}

static int case_fallocate(struct test_ctx *ctx)
{
	char dir[128], path[160];
	struct stat st;
	int fd, err;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "fallocate") != 0)
		return test_fail_msg("fallocate: cannot create temp dir");
	if (make_path(path, sizeof(path), dir, "file") != 0)
		return -1;

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("fallocate: open failed: %d", errno);

	err = posix_fallocate(fd, 0, 8192);
	if (err)
		return test_fail_msg("fallocate: posix_fallocate failed: %d", err);
	if (fstat(fd, &st) != 0 || st.st_size != 8192)
		return test_fail_msg("fallocate: size after posix_fallocate is %ld",
				     (long)st.st_size);

	err = posix_fallocate(fd, 0, 0);
	if (err != EINVAL)
		return test_fail_msg("fallocate: zero length returned %d", err);

	if (fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, 16384) != 0)
		return test_fail_msg("fallocate: KEEP_SIZE failed: %d", errno);
	if (fstat(fd, &st) != 0 || st.st_size != 8192)
		return test_fail_msg("fallocate: KEEP_SIZE changed size to %ld",
				     (long)st.st_size);

	if (fallocate(fd, 0, 8192, 4096) != 0)
		return test_fail_msg("fallocate: extend failed: %d", errno);
	if (fstat(fd, &st) != 0 || st.st_size != 12288)
		return test_fail_msg("fallocate: size after extend is %ld",
				     (long)st.st_size);

	close(fd);
	unlink(path);
	rmdir(dir);
	return 0;
}

static const struct test_case file_cases[] = {
	{ "basic", case_basic, TEST_OWN_PGRP, 10000 },
	{ "append", case_append, TEST_OWN_PGRP, 10000 },
	{ "truncate", case_truncate, TEST_OWN_PGRP, 10000 },
	{ "fallocate", case_fallocate, TEST_OWN_PGRP, 10000 },
	{ "open_flags", case_open_flags, TEST_OWN_PGRP, 10000 },
	{ "rename", case_rename, TEST_OWN_PGRP, 10000 },
	{ "dir_ops", case_dir_ops, TEST_OWN_PGRP, 10000 },
};

const struct test_suite brktest_file_suite = {
	.name = "file",
	.description = "regular file I/O, flags and directory operations",
	.cases = file_cases,
	.nr_cases = sizeof(file_cases) / sizeof(file_cases[0]),
};
