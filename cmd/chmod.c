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

static const char usage[] =
	"Usage: chmod [-R] MODE FILE...\n"
	"Change the mode of each FILE.\n"
	"\n"
	"Options:\n"
	"  -R, --recursive   change files and directories recursively\n"
	"  --help            display this help and exit\n"
	"\n"
	"MODE is an octal number (e.g. 755) or a symbolic mode\n"
	"such as u+x, go-w or a=r.";

static int join_path(char *out, size_t size, const char *dir, const char *name)
{
	int n = snprintf(out, size, "%s/%s", dir, name);

	if (n < 0 || (size_t)n >= size) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return 0;
}

static mode_t who_mask(int who)
{
	mode_t mask = 0;

	if (who & 4)
		mask |= 0700;
	if (who & 2)
		mask |= 0070;
	if (who & 1)
		mask |= 0007;
	return mask;
}

static int apply_symbolic(const char *spec, mode_t oldmode, mode_t *out)
{
	mode_t mode = oldmode & 07777;

	for (const char *p = spec; *p;) {
		int who = 0;
		bool who_given = false;

		while (*p && strchr("ugoa", *p)) {
			who_given = true;
			if (*p == 'u')
				who |= 4;
			else if (*p == 'g')
				who |= 2;
			else if (*p == 'o')
				who |= 1;
			else
				who |= 7;
			p++;
		}
		if (!who_given)
			who = 7;

		mode_t wmask = who_mask(who);
		bool op_seen = false;

		while (*p && *p != ',') {
			char op = *p++;

			if (op != '+' && op != '-' && op != '=')
				return -1;
			op_seen = true;

			mode_t perm = 0;

			while (*p && *p != '+' && *p != '-' && *p != '=' &&
			       *p != ',') {
				mode_t bit;

				switch (*p) {
				case 'r':
					bit = wmask & 0444;
					break;
				case 'w':
					bit = wmask & 0222;
					break;
				case 'x':
					bit = wmask & 0111;
					break;
				case 'X':
					bit = (S_ISDIR(oldmode) ||
					       (oldmode & 0111)) ?
						      (wmask & 0111) :
						      0;
					break;
				default:
					return -1;
				}
				perm |= bit;
				p++;
			}

			if (op == '+')
				mode |= perm;
			else if (op == '-')
				mode &= ~perm;
			else
				mode = (mode & ~wmask) | perm;
		}

		if (!op_seen)
			return -1;

		if (*p == ',')
			p++;
		else if (*p)
			return -1;
	}

	*out = mode & 07777;
	return 0;
}

static int parse_octal(const char *s, mode_t *out)
{
	unsigned long v = 0;
	const char *p = s;

	if (!*p)
		return -1;

	for (; *p; p++) {
		if (*p < '0' || *p > '7')
			return -1;
		v = (v << 3) | (unsigned long)(*p - '0');
		if (v > 07777)
			return -1;
	}

	*out = (mode_t)v;
	return 0;
}

static int do_chmod(const char *path, const char *spec, bool octal,
		    mode_t oct_mode)
{
	mode_t mode;

	if (!octal) {
		struct stat st;

		if (stat(path, &st) != 0) {
			brk_cmd_error("%s: %s", path, strerror(errno));
			return -1;
		}
		if (apply_symbolic(spec, st.st_mode, &mode) != 0) {
			brk_cmd_error("invalid mode '%s'", spec);
			return -1;
		}
	} else {
		mode = oct_mode;
	}

	if (chmod(path, mode) != 0) {
		brk_cmd_error("%s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

static int chmod_tree(const char *path, const char *spec, bool octal,
		      mode_t oct_mode, bool recursive)
{
	struct stat st;

	if (do_chmod(path, spec, octal, oct_mode) != 0)
		return -1;

	if (!recursive)
		return 0;

	if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
		return 0;

	int fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) {
		brk_cmd_error("%s: %s", path, strerror(errno));
		return -1;
	}

	char dbuf[4096];
	size_t used = 0;
	int ret = 0;

	for (;;) {
		ssize_t rcnt =
			posix_getdents(fd, dbuf + used, sizeof(dbuf) - used, 0);

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			brk_cmd_error("%s: %s", path, strerror(errno));
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
			if (strcmp(d->d_name, ".") && strcmp(d->d_name, "..") &&
			    d->d_type != DT_LNK) {
				char child[PATH_MAX];

				if (join_path(child, sizeof(child), path,
					      d->d_name) != 0) {
					brk_cmd_error("%s: path too long",
						      path);
					ret = -1;
				} else if (chmod_tree(child, spec, octal,
						      oct_mode, true) != 0) {
					ret = -1;
				}
			}
			pos += d->d_reclen;
			d = (struct posix_dent *)((char *)d + d->d_reclen);
		}
		used = 0;
	}

	close(fd);
	return ret;
}

int main(int argc, char *argv[])
{
	const char *spec;
	bool recursive = false;
	bool octal;
	mode_t oct_mode = 0;
	int i = 1;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);

	while (i < argc) {
		if (!strcmp(argv[i], "--help"))
			brk_cmd_help_exit(usage);
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
		brk_cmd_usage(usage);

	spec = argv[i++];
	octal = parse_octal(spec, &oct_mode) == 0;

	if (!octal) {
		mode_t dummy;

		if (apply_symbolic(spec, 0, &dummy) != 0)
			brk_cmd_usage(usage);
	}

	for (; i < argc; i++) {
		if (chmod_tree(argv[i], spec, octal, oct_mode, recursive) != 0)
			ret = BRK_CMD_EXIT_FAIL;
	}

	return ret;
}
