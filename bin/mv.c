#include <brk/cmd.h>
#include <brk/limits.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] = "Usage: mv SOURCE... DEST\n"
			    "Move or rename files and directories.";

static const struct brk_cmd_option opts[] = {
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int write_all(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t n = write(fd, buf, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0) {
			errno = EIO;
			return -1;
		}
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

static int join_path(char *out, size_t size, const char *dir, const char *name)
{
	int n = snprintf(out, size, "%s/%s", dir, name);

	if (n < 0 || (size_t)n >= size) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

static int copy_file(const char *src, const char *dest)
{
	char buf[4096];
	struct stat st;
	int in = -1, out = -1;
	int ret = -1;

	if (stat(src, &st) != 0)
		return brk_cmd_fail_errno("stat failed");

	in = open(src, O_RDONLY);
	if (in < 0)
		return brk_cmd_fail_errno("open failed");

	out = open(dest, O_WRONLY | O_CREAT | O_TRUNC,
		   st.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
	if (out < 0) {
		brk_cmd_error_errno("open failed");
		goto out;
	}

	for (;;) {
		ssize_t rcnt = read(in, buf, sizeof(buf));

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			brk_cmd_error_errno("read failed");
			goto out;
		}
		if (rcnt == 0)
			break;
		if (write_all(out, buf, (size_t)rcnt)) {
			brk_cmd_error_errno("write failed");
			goto out;
		}
	}
	ret = 0;

out:
	if (in >= 0)
		close(in);
	if (out >= 0)
		close(out);
	return ret;
}

static int copy_tree(const char *src, const char *dest);

static int copy_dir(const char *src, const char *dest)
{
	char dbuf[4096];
	size_t used = 0;
	int fd, ret = -1;

	if (mkdir(dest, 0755) != 0 && errno != EEXIST)
		return brk_cmd_fail_errno("mkdir failed");

	fd = open(src, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return brk_cmd_fail_errno("open directory failed");

	for (;;) {
		ssize_t rcnt =
			posix_getdents(fd, dbuf + used, sizeof(dbuf) - used, 0);

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			brk_cmd_error_errno("posix_getdents failed");
			goto out;
		}
		if (rcnt == 0)
			break;
		used += (size_t)rcnt;

		struct posix_dent *d = (struct posix_dent *)dbuf;
		size_t pos = 0;

		while (pos < used) {
			if (d->d_reclen == 0)
				break;
			if (strcmp(d->d_name, ".") && strcmp(d->d_name, "..")) {
				char sp[PATH_MAX], dp[PATH_MAX];

				if (join_path(sp, sizeof(sp), src, d->d_name) ||
				    join_path(dp, sizeof(dp), dest,
					      d->d_name)) {
					brk_cmd_error_errno("path too long");
					goto out;
				}
				if (copy_tree(sp, dp) != 0) {
					ret = -1;
					goto out;
				}
			}
			pos += d->d_reclen;
			d = (struct posix_dent *)((char *)d + d->d_reclen);
		}
		used = 0;
	}
	ret = 0;

out:
	close(fd);
	return ret;
}

static int copy_tree(const char *src, const char *dest)
{
	struct stat st;

	if (stat(src, &st) != 0)
		return brk_cmd_fail_errno("stat failed");

	if (S_ISDIR(st.st_mode))
		return copy_dir(src, dest);
	return copy_file(src, dest);
}

static int remove_tree(const char *path)
{
	struct stat st;
	char dbuf[4096], lbuf[PATH_MAX];
	size_t used = 0;
	int fd, ret = 0;

	if (readlink(path, lbuf, sizeof(lbuf)) >= 0)
		return unlink(path) == 0 ? 0 :
					   brk_cmd_fail_errno("remove failed");

	if (stat(path, &st) != 0)
		return brk_cmd_fail_errno("stat failed");

	if (!S_ISDIR(st.st_mode))
		return unlink(path) == 0 ? 0 :
					   brk_cmd_fail_errno("remove failed");

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return brk_cmd_fail_errno("open directory failed");

	for (;;) {
		ssize_t rcnt =
			posix_getdents(fd, dbuf + used, sizeof(dbuf) - used, 0);

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			brk_cmd_error_errno("posix_getdents failed");
			ret = -1;
			break;
		}
		if (rcnt == 0)
			break;
		used += (size_t)rcnt;

		struct posix_dent *d = (struct posix_dent *)dbuf;
		size_t pos = 0;

		while (pos < used) {
			if (d->d_reclen == 0)
				break;
			if (strcmp(d->d_name, ".") && strcmp(d->d_name, "..")) {
				char child[PATH_MAX];

				if (join_path(child, sizeof(child), path,
					      d->d_name) != 0) {
					brk_cmd_error_errno("path too long");
					ret = -1;
				} else if (remove_tree(child) != 0) {
					ret = -1;
				}
			}
			pos += d->d_reclen;
			d = (struct posix_dent *)((char *)d + d->d_reclen);
		}
		used = 0;
	}
	close(fd);

	if (rmdir(path) != 0)
		ret = brk_cmd_fail_errno("remove directory failed");

	return ret;
}

static int move_one(const char *src, const char *dest)
{
	if (rename(src, dest) == 0)
		return 0;

	if (errno != EXDEV)
		return brk_cmd_fail_errno("cannot move");

	if (copy_tree(src, dest) != 0)
		return BRK_CMD_EXIT_FAIL;

	if (remove_tree(src) != 0) {
		remove_tree(dest);
		return BRK_CMD_EXIT_FAIL;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	struct stat dst;
	char target[PATH_MAX];
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
	}

	if (brk_cmd_operand_count(&ctx) < 2)
		brk_cmd_usage(usage);

	const char *dest =
		brk_cmd_operand(&ctx, brk_cmd_operand_count(&ctx) - 1);
	bool dest_is_dir = stat(dest, &dst) == 0 && S_ISDIR(dst.st_mode);

	if (brk_cmd_operand_count(&ctx) > 2 && !dest_is_dir)
		brk_cmd_usage(usage);

	for (int i = 0; i < brk_cmd_operand_count(&ctx) - 1; i++) {
		const char *src = brk_cmd_operand(&ctx, i);
		const char *out = dest;

		if (dest_is_dir) {
			if (join_path(target, sizeof(target), dest,
				      brk_cmd_basename(src)) != 0) {
				brk_cmd_error_errno("path too long");
				ret = BRK_CMD_EXIT_FAIL;
				continue;
			}
			out = target;
		}

		if (move_one(src, out) != 0)
			ret = BRK_CMD_EXIT_FAIL;
	}

	return ret;
}
