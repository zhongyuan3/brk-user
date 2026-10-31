#include <apputil.h>
#include <brk/limits.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] =
	"Usage: chgrp [-R] GROUP FILE...\n"
	"Change the group of each FILE.\n"
	"GROUP must be a numeric ID.\n"
	"\n"
	"Options:\n"
	"  -R, --recursive   change files and directories recursively\n"
	"  --help            display this help and exit";

static int join_path(char *out, size_t size, const char *dir, const char *name)
{
	int n = snprintf(out, size, "%s/%s", dir, name);

	if (n < 0 || (size_t)n >= size) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

static int chgrp_tree(const char *path, gid_t gid, bool recursive)
{
	struct stat st;

	if (chown(path, (uid_t)-1, gid) != 0) {
		app_error("%s: %s", path, strerror(errno));
		return -1;
	}

	if (!recursive)
		return 0;

	if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
		return 0;

	int fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		app_error("%s: %s", path, strerror(errno));
		return -1;
	}

	char dbuf[4096];
	size_t used = 0;
	int ret = 0;

	for (;;) {
		ssize_t rcnt = getdents64(fd, dbuf + used, sizeof(dbuf) - used);

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			app_error("%s: %s", path, strerror(errno));
			ret = -1;
			break;
		}
		if (rcnt == 0)
			break;
		used += (size_t)rcnt;

		struct dirent64 *d = (struct dirent64 *)dbuf;
		size_t pos = 0;

		while (pos < used) {
			if (d->d_reclen == 0)
				break;
			if (strcmp(d->d_name, ".") && strcmp(d->d_name, "..") &&
			    d->d_type != DT_LNK) {
				char child[PATH_MAX];

				if (join_path(child, sizeof(child), path,
					      d->d_name) != 0) {
					app_error("%s: path too long", path);
					ret = -1;
				} else if (chgrp_tree(child, gid, true) != 0) {
					ret = -1;
				}
			}
			pos += d->d_reclen;
			d = (struct dirent64 *)((char *)d + d->d_reclen);
		}
		used = 0;
	}

	close(fd);
	return ret;
}

int main(int argc, char *argv[])
{
	bool recursive = false;
	unsigned long gid;
	int i = 1;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);

	while (i < argc) {
		if (!strcmp(argv[i], "--help"))
			app_help_exit(usage);
		if (!strcmp(argv[i], "-R") || !strcmp(argv[i], "--recursive")) {
			recursive = true;
			i++;
			continue;
		}
		if (!strcmp(argv[i], "--")) {
			i++;
			break;
		}
		break;
	}

	if (argc - i < 2)
		app_usage(usage);

	if (app_parse_ulong(argv[i++], &gid, 0, 0xffffffffUL) != 0)
		app_usage(usage);

	for (; i < argc; i++) {
		if (chgrp_tree(argv[i], (gid_t)gid, recursive) != 0)
			ret = APP_EXIT_FAIL;
	}

	return ret;
}
