#include <brk/cmd.h>
#include <brk/limits.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] =
	"Usage: find [PATH...] [-name PATTERN] [-type TYPE] [-maxdepth N]\n"
	"            [-mindepth N] [-print]\n"
	"Search for files in a directory hierarchy.";

struct find_opts {
	const char *name;
	char type;
	int maxdepth;
	int mindepth;
	bool print;
};

static bool glob_match(const char *pat, const char *str)
{
	while (*pat) {
		if (*pat == '*') {
			while (*pat == '*')
				pat++;
			if (!*pat)
				return true;
			for (const char *s = str;; s++) {
				if (glob_match(pat, s))
					return true;
				if (!*s)
					return false;
			}
		}
		if (*pat == '?') {
			if (!*str)
				return false;
			pat++;
			str++;
			continue;
		}
		if (*pat == '[') {
			bool negate = false;
			bool matched = false;
			const char *p = pat + 1;

			if (*p == '!' || *p == '^') {
				negate = true;
				p++;
			}
			if (!*str)
				return false;
			while (*p && *p != ']') {
				char lo = *p++;
				char hi = lo;

				if (*p == '-' && p[1] && p[1] != ']') {
					p++;
					hi = *p++;
				}
				if (*str >= lo && *str <= hi)
					matched = true;
			}
			if (*p != ']')
				return false;
			if (matched == negate)
				return false;
			pat = p + 1;
			str++;
			continue;
		}
		if (*pat == '\\' && pat[1])
			pat++;
		if (*pat != *str)
			return false;
		pat++;
		str++;
	}
	return *str == '\0';
}

static bool type_matches(mode_t mode, char type)
{
	switch (type) {
	case 'f':
		return S_ISREG(mode);
	case 'd':
		return S_ISDIR(mode);
	case 'l':
		return S_ISLNK(mode);
	case 'c':
		return S_ISCHR(mode);
	case 'b':
		return S_ISBLK(mode);
	case 'p':
		return S_ISFIFO(mode);
	case 's':
		return S_ISSOCK(mode);
	default:
		return false;
	}
}

static int find_walk(const char *path, int depth, const struct find_opts *o)
{
	struct stat st;
	const char *base;
	bool match;
	int ret = 0;

	if (lstat(path, &st) != 0) {
		brk_cmd_error("%s: %s", path, strerror(errno));
		return 1;
	}

	match = depth >= o->mindepth;
	if (match && o->name) {
		base = strrchr(path, '/');
		base = base ? base + 1 : path;
		match = glob_match(o->name, base);
	}
	if (match && o->type)
		match = type_matches(st.st_mode, o->type);

	if (match && o->print)
		puts(path);

	if (!S_ISDIR(st.st_mode) || depth >= o->maxdepth)
		return 0;

	int fd = open(path, O_RDONLY | O_DIRECTORY);

	if (fd < 0) {
		brk_cmd_error("%s: %s", path, strerror(errno));
		return 1;
	}

	char dbuf[4096];
	size_t used = 0;

	for (;;) {
		ssize_t n =
			posix_getdents(fd, dbuf + used, sizeof(dbuf) - used, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			brk_cmd_error("%s: %s", path, strerror(errno));
			ret = 1;
			break;
		}
		if (n == 0)
			break;
		used += (size_t)n;

		struct posix_dent *d = (struct posix_dent *)dbuf;
		size_t pos = 0;

		while (pos < used) {
			if (d->d_reclen == 0)
				break;
			if (strcmp(d->d_name, ".") && strcmp(d->d_name, "..")) {
				char child[PATH_MAX];
				int len = snprintf(child, sizeof(child),
						   "%s/%s", path, d->d_name);

				if (len < 0 || (size_t)len >= sizeof(child)) {
					brk_cmd_error("%s: path too long",
						      path);
					ret = 1;
				} else {
					ret |= find_walk(child, depth + 1, o);
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
	struct find_opts o = {
		.maxdepth = 0x7fffffff,
		.mindepth = 0,
		.print = true,
	};
	const char *paths[64];
	int npaths = 0;
	int i = 1;
	int ret = 0;

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(usage);

	while (i < argc && argv[i][0] != '-') {
		if (npaths >= (int)(sizeof(paths) / sizeof(paths[0])))
			brk_cmd_usage(usage);
		paths[npaths++] = argv[i++];
	}

	while (i < argc) {
		const char *arg = argv[i++];

		if (!strcmp(arg, "-name")) {
			if (i >= argc)
				brk_cmd_usage(usage);
			o.name = argv[i++];
		} else if (!strcmp(arg, "-type")) {
			if (i >= argc || !argv[i][0] || argv[i][1])
				brk_cmd_usage(usage);
			o.type = argv[i++][0];
		} else if (!strcmp(arg, "-maxdepth")) {
			long v;

			if (i >= argc || brk_cmd_parse_long(argv[i++], &v, 0,
							    0x7fffffff) != 0)
				brk_cmd_usage(usage);
			o.maxdepth = (int)v;
		} else if (!strcmp(arg, "-mindepth")) {
			long v;

			if (i >= argc || brk_cmd_parse_long(argv[i++], &v, 0,
							    0x7fffffff) != 0)
				brk_cmd_usage(usage);
			o.mindepth = (int)v;
		} else if (!strcmp(arg, "-print")) {
			o.print = true;
		} else {
			brk_cmd_usage(usage);
		}
	}

	if (npaths == 0)
		paths[npaths++] = ".";

	for (int p = 0; p < npaths; p++)
		ret |= find_walk(paths[p], 0, &o);

	return ret ? BRK_CMD_EXIT_FAIL : BRK_CMD_EXIT_OK;
}
