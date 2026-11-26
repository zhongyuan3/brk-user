#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int make_path(char *path, size_t size, const char *dir,
		     const char *name)
{
	int n = snprintf(path, size, "%s/%s", dir, name);

	return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

static int case_stat(struct test_ctx *ctx)
{
	char dir[128], path[160];
	struct stat st;
	struct stat fst;
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "fsmeta") != 0)
		return test_fail_msg("fsmeta: cannot create temp dir");
	if (make_path(path, sizeof(path), dir, "file") != 0)
		return -1;
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0640);
	if (fd < 0)
		return test_fail_msg("fsmeta: open failed: %d", errno);
	if (write(fd, "abc", 3) != 3)
		return test_fail_msg("fsmeta: write failed: %d", errno);
	if (fstat(fd, &fst) != 0)
		return test_fail_msg("fsmeta: fstat failed: %d", errno);
	if (stat(path, &st) != 0)
		return test_fail_msg("fsmeta: stat failed: %d", errno);
	if (!S_ISREG(st.st_mode) || st.st_size != 3 ||
	    st.st_nlink != 1 || st.st_ino != fst.st_ino ||
	    st.st_dev != fst.st_dev || st.st_uid != geteuid() ||
	    st.st_gid != getegid())
		return test_fail_msg("fsmeta: regular file stat is wrong");
	if ((st.st_mode & 07777) != 0640)
		return test_fail_msg("fsmeta: create mode is %o",
				     st.st_mode & 07777);
	close(fd);
	unlink(path);
	rmdir(dir);
	return 0;
}

static int case_links(struct test_ctx *ctx)
{
	char dir[128], first[160], second[160], linkpath[160];
	struct stat st;
	struct stat lst;
	char target[16];
	ssize_t n;
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "links") != 0)
		return test_fail_msg("fsmeta: cannot create temp dir");
	if (make_path(first, sizeof(first), dir, "first") != 0 ||
	    make_path(second, sizeof(second), dir, "second") != 0 ||
	    make_path(linkpath, sizeof(linkpath), dir, "link") != 0)
		return -1;

	fd = open(first, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("fsmeta: open failed: %d", errno);
	if (write(fd, "data", 4) != 4)
		return test_fail_msg("fsmeta: write failed: %d", errno);
	close(fd);
	if (link(first, second) != 0)
		return test_fail_msg("fsmeta: link failed: %d", errno);
	if (stat(second, &st) != 0 || st.st_nlink != 2)
		return test_fail_msg("fsmeta: link count is wrong");
	if (stat(first, &st) != 0 || st.st_nlink != 2)
		return test_fail_msg("fsmeta: original link count is wrong");

	if (symlink("first", linkpath) != 0)
		return test_fail_msg("fsmeta: symlink failed: %d", errno);
	if (lstat(linkpath, &lst) != 0 || !S_ISLNK(lst.st_mode))
		return test_fail_msg("fsmeta: lstat did not report a symlink");
	if (stat(linkpath, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size != 4)
		return test_fail_msg("fsmeta: stat did not follow symlink");
	n = readlink(linkpath, target, sizeof(target));
	if (n != 5 || memcmp(target, "first", 5))
		return test_fail_msg("fsmeta: readlink returned %ld", (long)n);

	if (unlink(first) != 0)
		return test_fail_msg("fsmeta: unlink failed: %d", errno);
	if (stat(second, &st) != 0 || st.st_nlink != 1)
		return test_fail_msg("fsmeta: hard link did not survive unlink");
	errno = 0;
	if (stat(first, &st) != -1 || errno != ENOENT)
		return test_fail_msg("fsmeta: unlinked path still exists");
	if (lstat(linkpath, &lst) != 0 || !S_ISLNK(lst.st_mode))
		return test_fail_msg("fsmeta: dangling symlink disappeared");
	errno = 0;
	if (stat(linkpath, &st) != -1 || errno != ENOENT)
		return test_fail_msg("fsmeta: dangling symlink resolved");

	unlink(second);
	unlink(linkpath);
	rmdir(dir);
	return 0;
}

static int case_meta(struct test_ctx *ctx)
{
	char dir[128], path[160];
	struct stat st;
	struct utimbuf ut;
	struct timespec ts[2] = {
		{ .tv_sec = 1234567890, .tv_nsec = 0 },
		{ .tv_sec = 1234567890, .tv_nsec = 0 },
	};
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "meta") != 0)
		return test_fail_msg("fsmeta: cannot create temp dir");
	if (make_path(path, sizeof(path), dir, "file") != 0)
		return -1;
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("fsmeta: open failed: %d", errno);
	if (fchmod(fd, 0600) != 0)
		return test_fail_msg("fsmeta: fchmod failed: %d", errno);
	if (fstat(fd, &st) != 0 || (st.st_mode & 07777) != 0600)
		return test_fail_msg("fsmeta: fchmod mode is wrong");

	if (chmod(path, 0640) != 0)
		return test_fail_msg("fsmeta: chmod failed: %d", errno);
	if (stat(path, &st) != 0 || (st.st_mode & 07777) != 0640)
		return test_fail_msg("fsmeta: chmod mode is wrong");
	if (chown(path, 1000, 1000) != 0)
		return test_fail_msg("fsmeta: chown failed: %d", errno);
	if (stat(path, &st) != 0 || st.st_uid != 1000 || st.st_gid != 1000)
		return test_fail_msg("fsmeta: chown ids are wrong");

	ut.actime = 1000000000;
	ut.modtime = 1000000000;
	if (utime(path, &ut) != 0)
		return test_fail_msg("fsmeta: utime failed: %d", errno);
	if (stat(path, &st) != 0 || st.st_atime != 1000000000 ||
	    st.st_mtime != 1000000000)
		return test_fail_msg("fsmeta: utime timestamps are wrong");
	if (utimensat(AT_FDCWD, path, ts, 0) != 0)
		return test_fail_msg("fsmeta: utimensat failed: %d", errno);
	if (stat(path, &st) != 0 || st.st_atime != 1234567890 ||
	    st.st_mtime != 1234567890)
		return test_fail_msg("fsmeta: utimensat timestamps are wrong");

	if (fsync(fd) != 0)
		return test_fail_msg("fsmeta: fsync failed: %d", errno);
	close(fd);
	unlink(path);
	rmdir(dir);
	return 0;
}

static int case_unlink_open(struct test_ctx *ctx)
{
	char dir[128], path[160];
	char buf[4];
	struct stat st;
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "unlink") != 0)
		return test_fail_msg("fsmeta: cannot create temp dir");
	if (make_path(path, sizeof(path), dir, "file") != 0)
		return -1;
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("fsmeta: open failed: %d", errno);
	if (write(fd, "keep", 4) != 4)
		return test_fail_msg("fsmeta: write failed: %d", errno);
	if (unlink(path) != 0)
		return test_fail_msg("fsmeta: unlink failed: %d", errno);
	errno = 0;
	if (stat(path, &st) != -1 || errno != ENOENT)
		return test_fail_msg("fsmeta: unlinked path is still visible");
	if (lseek(fd, 0, SEEK_SET) != 0)
		return test_fail_msg("fsmeta: lseek failed");
	if (read(fd, buf, sizeof(buf)) != 4 || memcmp(buf, "keep", 4))
		return test_fail_msg("fsmeta: open file lost data after unlink");
	if (fstat(fd, &st) != 0 || st.st_nlink != 0)
		return test_fail_msg("fsmeta: open unlinked file link count is %u",
				     (unsigned)st.st_nlink);
	close(fd);
	rmdir(dir);
	return 0;
}

static int case_rename(struct test_ctx *ctx)
{
	char dir[128], oldpath[160], newpath[160];
	char buf[8] = { 0 };
	struct stat st;
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "rename") != 0)
		return test_fail_msg("fsmeta: cannot create temp dir");
	if (make_path(oldpath, sizeof(oldpath), dir, "old") != 0 ||
	    make_path(newpath, sizeof(newpath), dir, "new") != 0)
		return -1;

	fd = open(oldpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("fsmeta: open old failed: %d", errno);
	if (write(fd, "old", 3) != 3)
		return test_fail_msg("fsmeta: write old failed");
	close(fd);
	fd = open(newpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("fsmeta: open new failed: %d", errno);
	if (write(fd, "new", 3) != 3)
		return test_fail_msg("fsmeta: write new failed");
	close(fd);

	if (rename(oldpath, newpath) != 0)
		return test_fail_msg("fsmeta: rename failed: %d", errno);
	errno = 0;
	if (stat(oldpath, &st) != -1 || errno != ENOENT)
		return test_fail_msg("fsmeta: rename left the old path");
	fd = open(newpath, O_RDONLY);
	if (fd < 0)
		return test_fail_msg("fsmeta: renamed file is missing");
	if (read(fd, buf, sizeof(buf)) != 3 || memcmp(buf, "old", 3))
		return test_fail_msg("fsmeta: rename did not replace the target");
	close(fd);
	unlink(newpath);

	errno = 0;
	if (rename(oldpath, newpath) != -1 || errno != ENOENT)
		return test_fail_msg("fsmeta: rename missing did not return ENOENT");
	rmdir(dir);
	return 0;
}

static const struct test_case fsmeta_cases[] = {
	{ "stat", case_stat, TEST_OWN_PGRP, 10000 },
	{ "links", case_links, TEST_OWN_PGRP, 10000 },
	{ "meta", case_meta, TEST_OWN_PGRP, 10000 },
	{ "unlink_open", case_unlink_open, TEST_OWN_PGRP, 10000 },
	{ "rename", case_rename, TEST_OWN_PGRP, 10000 },
};

const struct test_suite brktest_fsmeta_suite = {
	.name = "fsmeta",
	.description = "file metadata, links, symlinks and timestamps",
	.cases = fsmeta_cases,
	.nr_cases = sizeof(fsmeta_cases) / sizeof(fsmeta_cases[0]),
};
