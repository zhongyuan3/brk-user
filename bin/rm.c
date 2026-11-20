#include <libcmdfw.h>
#include <brk/limits.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] = "Usage: rm [-f] [-r] FILE...\n"
			    "Remove FILE(s).\n"
			    "\n"
			    "Options:\n"
			    "  -f       ignore missing files\n"
			    "  -r       remove directories recursively\n"
			    "  --help   display this help and exit";

static const struct cmdfw_option opts[] = {
	{ 'f', 'f', "force", CMDFW_OPT_NO_ARG },
	{ 'r', 'r', "recursive", CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

static int join_path(char *out, size_t size, const char *dir, const char *name)
{
	int n = snprintf(out, size, "%s/%s", dir, name);

	if (n < 0 || (size_t)n >= size) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

static int remove_path(const char *path, bool recursive, bool force)
{
	struct stat st;
	char lbuf[PATH_MAX];
	char buf[4096];
	size_t used = 0;
	int fd;
	int ret = 0;

	/* Do not follow a symlink: remove the link itself. */
	if (readlink(path, lbuf, sizeof(lbuf)) >= 0) {
		if (unlink(path) != 0)
			return cmdfw_fail_errno("cannot remove");
		return 0;
	}

	if (stat(path, &st) != 0) {
		if (force && errno == ENOENT)
			return 0;
		return cmdfw_fail_errno("cannot stat");
	}

	if (!S_ISDIR(st.st_mode)) {
		if (unlink(path) != 0)
			return cmdfw_fail_errno("cannot remove");
		return 0;
	}

	if (!recursive)
		return cmdfw_fail("cannot remove directory '%s' without -r",
				    path);

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return cmdfw_fail_errno("cannot open directory");

	for (;;) {
		ssize_t rcnt =
			posix_getdents(fd, buf + used, sizeof(buf) - used, 0);

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			cmdfw_error_errno("posix_getdents failed");
			ret = -1;
			break;
		}
		if (rcnt == 0)
			break;
		used += (size_t)rcnt;

		struct posix_dent *d = (struct posix_dent *)buf;
		size_t pos = 0;

		while (pos < used) {
			if (d->d_reclen == 0)
				break;
			if (strcmp(d->d_name, ".") && strcmp(d->d_name, "..")) {
				char child[PATH_MAX];

				if (join_path(child, sizeof(child), path,
					      d->d_name) != 0) {
					cmdfw_error_errno("path too long");
					ret = -1;
				} else if (remove_path(child, recursive,
						       force) != 0) {
					ret = -1;
				}
			}
			pos += d->d_reclen;
			d = (struct posix_dent *)((char *)d + d->d_reclen);
		}
		used = 0;
	}
	close(fd);

	if (rmdir(path) != 0) {
		if (!(force && errno == ENOENT))
			ret = cmdfw_fail_errno("cannot remove directory");
	}
	return ret;
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	bool force = false;
	bool recursive = false;
	int opt;
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'f')
			force = true;
		if (opt == 'r')
			recursive = true;
	}

	if (cmdfw_operand_count(&ctx) == 0)
		cmdfw_usage(usage);

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		if (remove_path(cmdfw_operand(&ctx, i), recursive, force) !=
		    0)
			ret = CMDFW_EXIT_FAIL;
	}

	return ret;
}
