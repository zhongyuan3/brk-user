#include <apputil.h>
#include <brk/limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

struct ls_args {
	bool long_format;
	bool all;
	bool human_readable;
};

static char *dup_name(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);

	if (!p)
		return NULL;
	memcpy(p, s, n);
	return p;
}

static int cmp_strp(const void *a, const void *b)
{
	const char *const *x = a;
	const char *const *y = b;
	return strcmp(*x, *y);
}

/* Returns 0 and sets *cols on success; -1 if ioctl fails (caller: one name per line). */
static int get_term_cols(unsigned *cols)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0)
		return -1;
	*cols = ws.ws_col;
	if (*cols < 1)
		*cols = 1;
	return 0;
}

static void mode_string(unsigned mode, char out[11])
{
	memset(out, '-', 10);
	out[10] = '\0';

	if (S_ISDIR(mode))
		out[0] = 'd';
	else if (S_ISREG(mode))
		out[0] = '-';
	else if (S_ISLNK(mode))
		out[0] = 'l';
	else if (S_ISCHR(mode))
		out[0] = 'c';
	else if (S_ISBLK(mode))
		out[0] = 'b';
	else if (S_ISFIFO(mode))
		out[0] = 'p';
	else if (S_ISSOCK(mode))
		out[0] = 's';
	else
		out[0] = '?';

	if (mode & S_IRUSR)
		out[1] = 'r';
	if (mode & S_IWUSR)
		out[2] = 'w';
	if (mode & S_IXUSR)
		out[3] = 'x';
	if (mode & S_IRGRP)
		out[4] = 'r';
	if (mode & S_IWGRP)
		out[5] = 'w';
	if (mode & S_IXGRP)
		out[6] = 'x';
	if (mode & S_IROTH)
		out[7] = 'r';
	if (mode & S_IWOTH)
		out[8] = 'w';
	if (mode & S_IXOTH)
		out[9] = 'x';

	if (mode & S_ISUID)
		out[3] = (mode & S_IXUSR) ? 's' : 'S';
	if (mode & S_ISGID)
		out[6] = (mode & S_IXGRP) ? 's' : 'S';
	if (mode & S_ISVTX)
		out[9] = (mode & S_IXOTH) ? 't' : 'T';
}

/* GNU-style -h: 1024-based; <10 one decimal, else integer in current unit. */
static void format_size_human(long size, char *out, size_t outlen)
{
	static const char suf[] = "KMGTPEZY";
	unsigned long long n;
	double v;
	int si;

	if (size < 0)
		n = 0;
	else
		n = (unsigned long long)size;
	if (n < 1024) {
		snprintf(out, outlen, "%llu", (unsigned long long)n);
		return;
	}
	v = (double)n;
	si = -1;
	while (v >= 1024.0 && si < (int)sizeof(suf) - 2) {
		v /= 1024.0;
		si++;
	}
	if (v >= 1024.0) {
		snprintf(out, outlen, "%llu", (unsigned long long)n);
		return;
	}
	if (v < 10.0) {
		int t = (int)(v * 10.0 + 0.5);

		if (t >= 100) {
			snprintf(out, outlen, "%d%c", (int)(v + 0.5), suf[si]);
		} else {
			int ip = t / 10;
			int fp = t % 10;

			if (fp == 0)
				snprintf(out, outlen, "%d%c", ip, suf[si]);
			else
				snprintf(out, outlen, "%d.%d%c", ip, fp,
					 suf[si]);
		}
	} else {
		snprintf(out, outlen, "%d%c", (int)(v + 0.5), suf[si]);
	}
}

static int uint_field_width(unsigned int v)
{
	int w = 0;

	do {
		v /= 10u;
		w++;
	} while (v > 0u);
	return w ? w : 1;
}

static void format_size_field(long size, bool human_readable, char *buf,
			      size_t buflen)
{
	if (human_readable)
		format_size_human(size, buf, buflen);
	else
		snprintf(buf, buflen, "%ld", size);
}

/* Howard Hinnant's civil_from_days, same conversion used by date(1). */
static void civil_from_days(long z, int *year, unsigned int *month,
			    unsigned int *day)
{
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned int doe = (unsigned int)(z - era * 146097);
	unsigned int yoe =
		(doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int y = (int)yoe + (int)era * 400;
	unsigned int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned int mp = (5 * doy + 2) / 153;
	unsigned int d = doy - (153 * mp + 2) / 5 + 1;
	unsigned int m = mp < 10 ? mp + 3 : mp - 9;

	*year = y + (m <= 2);
	*month = m;
	*day = d;
}

static void format_time(long sec, char *buf, size_t buflen)
{
	long days = sec / 86400;
	long rem = sec % 86400;
	int year;
	unsigned int month, day;

	if (rem < 0) {
		rem += 86400;
		days--;
	}

	civil_from_days(days, &year, &month, &day);
	snprintf(buf, buflen, "%04d-%02u-%02u %02ld:%02ld:%02ld", year, month,
		 day, rem / 3600, (rem % 3600) / 60, rem % 60);
}

static void format_user(uid_t uid, char *buf, size_t buflen)
{
	struct passwd *pw = getpwuid(uid);

	if (pw)
		snprintf(buf, buflen, "%s", pw->pw_name);
	else
		snprintf(buf, buflen, "%u", (unsigned int)uid);
}

static void format_group(gid_t gid, char *buf, size_t buflen)
{
	struct group *gr = getgrgid(gid);

	if (gr)
		snprintf(buf, buflen, "%s", gr->gr_name);
	else
		snprintf(buf, buflen, "%u", (unsigned int)gid);
}

static void print_long_line(const char *path, const char *name,
			    const struct stat *st, bool human_readable,
			    int wnlink, int wuser, int wgroup, int wsize,
			    const char *user, const char *group,
			    const char *timestr)
{
	char mode[11];
	char szbuf[32];

	mode_string(st->st_mode, mode);
	format_size_field(st->st_size, human_readable, szbuf, sizeof(szbuf));
	printf("%s %*u %-*s %-*s %*s %s %s", mode, wnlink, st->st_nlink, wuser,
	       user, wgroup, group, wsize, szbuf, timestr, name);

	if (S_ISLNK(st->st_mode)) {
		char target[PATH_MAX];
		ssize_t n = readlink(path, target, sizeof(target) - 1);

		if (n >= 0) {
			target[n] = '\0';
			printf(" -> %s", target);
		}
	}
	putchar('\n');
}

static int ls_long(const char *path, char **names, size_t n,
		   bool human_readable)
{
	static char path_buf[512];
	size_t path_len = strlen(path);
	struct stat *sts = NULL;
	char (*users)[32] = NULL;
	char (*groups)[32] = NULL;
	char (*times)[32] = NULL;
	int wnlink = 1;
	int wuser = 1;
	int wgroup = 1;
	int wsize = 1;
	char szbuf[32];
	int ret = 1;

	memcpy(path_buf, path, path_len);
	char *sep;
	if (path_len > 0 && path_buf[path_len - 1] != '/') {
		path_buf[path_len] = '/';
		sep = path_buf + path_len + 1;
	} else {
		sep = path_buf + path_len;
	}
	size_t pb_len = sizeof(path_buf) - (size_t)(sep - path_buf);

	if (n) {
		sts = calloc(n, sizeof(*sts));
		users = calloc(n, sizeof(*users));
		groups = calloc(n, sizeof(*groups));
		times = calloc(n, sizeof(*times));
		if (!sts || !users || !groups || !times) {
			app_fail_errno("calloc failed");
			goto out;
		}
	}

	for (size_t i = 0; i < n; i++) {
		strlcpy(sep, names[i], pb_len);
		if (lstat(path_buf, &sts[i]) != 0) {
			app_fail_errno("lstat failed");
			goto out;
		}
		format_user(sts[i].st_uid, users[i], 32);
		format_group(sts[i].st_gid, groups[i], 32);
		format_time(sts[i].st_mtime, times[i], 32);
	}

	for (size_t i = 0; i < n; i++) {
		int w;
		const struct stat *st = &sts[i];

		w = uint_field_width(st->st_nlink);
		if (w > wnlink)
			wnlink = w;
		w = (int)strlen(users[i]);
		if (w > wuser)
			wuser = w;
		w = (int)strlen(groups[i]);
		if (w > wgroup)
			wgroup = w;
		format_size_field(st->st_size, human_readable, szbuf,
				  sizeof(szbuf));
		w = (int)strlen(szbuf);
		if (w > wsize)
			wsize = w;
	}

	/* GNU-style: sum of allocated blocks, printed as 1 KiB units. */

	unsigned long long n512 = 0;
	unsigned long long total_k;

	for (size_t i = 0; i < n; i++) {
		long b = sts[i].st_blocks;

		if (b > 0)
			n512 += (unsigned long long)b;
	}
	total_k = (n512 * 512ULL + 1023ULL) / 1024ULL;
	printf("total %llu\n", (unsigned long long)total_k);

	for (size_t i = 0; i < n; i++) {
		strlcpy(sep, names[i], pb_len);
		print_long_line(path_buf, names[i], &sts[i], human_readable,
				wnlink, wuser, wgroup, wsize, users[i],
				groups[i], times[i]);
	}
	ret = 0;

out:
	free(sts);
	free(users);
	free(groups);
	free(times);
	return ret;
}

/*
 * GNU default column layout: sorted names fill down column 0, then column 1,
 * … (index i sits at row i % nrows, column i / nrows).
 * Cell (row r, column c) is names[c * nrows + r] when that index is < n.
 */
static size_t col_max_width(size_t n, unsigned ncols, size_t nrows, unsigned c,
			    const size_t *w)
{
	size_t mw = 0;

	for (size_t r = 0; r < nrows; r++) {
		size_t idx = (size_t)c * nrows + r;

		if (idx < n && w[idx] > mw)
			mw = w[idx];
	}
	return mw;
}

static size_t line_width(size_t n, unsigned ncols, const size_t *w,
			 unsigned gap)
{
	size_t nrows = (n + ncols - 1) / ncols;
	size_t sum = 0;

	for (unsigned c = 0; c < ncols; c++) {
		sum += col_max_width(n, ncols, nrows, c, w);
		if (c + 1 < ncols)
			sum += gap;
	}
	return sum;
}

static void print_columns(char **names, size_t n, unsigned term_cols)
{
	const unsigned gap = 2;
	size_t *widths;
	size_t nrows;
	unsigned ncols;
	size_t lw;

	if (n == 0)
		return;

	widths = malloc(n * sizeof(*widths));
	if (!widths) {
		app_error_errno("malloc failed");
		return;
	}
	for (size_t i = 0; i < n; i++)
		widths[i] = strlen(names[i]);

	ncols = (unsigned)n;
	while (ncols > 1) {
		lw = line_width(n, ncols, widths, gap);
		if (lw <= term_cols)
			break;
		ncols--;
	}

	nrows = (n + ncols - 1) / ncols;

	for (size_t r = 0; r < nrows; r++) {
		bool out = false;

		for (unsigned c = 0; c < ncols; c++) {
			size_t idx = (size_t)c * nrows + r;

			if (idx >= n)
				continue;

			size_t colw = col_max_width(n, ncols, nrows, c, widths);
			bool more = false;

			for (unsigned d = c + 1; d < ncols; d++) {
				if ((size_t)d * nrows + r < n) {
					more = true;
					break;
				}
			}

			if (out)
				printf("%*s", (int)gap, "");
			out = true;
			printf("%s", names[idx]);
			if (more) {
				size_t pad = colw - widths[idx];

				while (pad--)
					putchar(' ');
			}
		}
		putchar('\n');
	}

	free(widths);
}

static int collect_names(int fd, bool all, char ***out_names, size_t *out_n)
{
	uint8_t *buf = NULL;
	size_t cap = 0;
	size_t len = 0;
	ssize_t r;
	char **names = NULL;
	size_t n = 0;
	size_t name_cap = 0;
	int ret = 0;

	for (;;) {
		if (len + 1024 > cap) {
			size_t new_cap = cap ? cap * 2 : 1024;
			void *nb = realloc(buf, new_cap);

			if (!nb) {
				ret = app_fail_errno("realloc failed");
				goto out;
			}
			buf = nb;
			cap = new_cap;
		}
		r = getdents64(fd, buf + len, cap - len);
		if (r < 0) {
			ret = app_fail_errno("getdents64 failed");
			goto out;
		}
		if (r == 0)
			break;
		len += (size_t)r;
	}

	struct dirent64 *p = (struct dirent64 *)buf;
	size_t i = 0;

	while (i < len) {
		const char *nm = p->d_name;

		if (!all && nm[0] == '.') {
			i += p->d_reclen;
			p = (struct dirent64 *)((uintptr_t)p + p->d_reclen);
			continue;
		}
		if (n + 1 > name_cap) {
			size_t nc = name_cap ? name_cap * 2 : 32;
			char **nn = realloc(names, nc * sizeof(*names));

			if (!nn) {
				ret = app_fail_errno("realloc failed");
				goto out;
			}
			names = nn;
			name_cap = nc;
		}
		names[n] = dup_name(nm);
		if (!names[n]) {
			ret = app_fail_errno("malloc failed");
			goto out;
		}
		n++;
		i += p->d_reclen;
		p = (struct dirent64 *)((uintptr_t)p + p->d_reclen);
	}

	*out_names = names;
	*out_n = n;
	names = NULL;
out:
	free(buf);
	if (names) {
		for (size_t k = 0; k < n; k++)
			free(names[k]);
		free(names);
	}
	return ret;
}

static int ls_dir(int fd, const char *path, const struct ls_args *args)
{
	char **names = NULL;
	size_t n = 0;
	int ret = 0;

	if (collect_names(fd, args->all, &names, &n) != 0) {
		ret = 1;
		goto done;
	}

	qsort(names, n, sizeof(*names), cmp_strp);

	if (args->long_format) {
		ret = ls_long(path, names, n, args->human_readable);
	} else {
		unsigned cols;

		if (get_term_cols(&cols) != 0) {
			for (size_t i = 0; i < n; i++)
				puts(names[i]);
		} else {
			print_columns(names, n, cols);
		}
	}

	for (size_t k = 0; k < n; k++)
		free(names[k]);
	free(names);

done:
	return ret;
}

static int ls_one(const char *path, const struct ls_args *args)
{
	int fd;
	int ret = 0;
	struct stat st;

	if (args->long_format) {
		if (lstat(path, &st) != 0)
			return app_fail_errno("lstat failed");

		if (S_ISDIR(st.st_mode)) {
			fd = open(path, O_RDONLY);
			if (fd < 0)
				return app_fail_errno("open failed");
			ret = ls_dir(fd, path, args);
			close(fd);
			return ret;
		}

		char user[32], group[32], timestr[32];

		format_user(st.st_uid, user, sizeof(user));
		format_group(st.st_gid, group, sizeof(group));
		format_time(st.st_mtime, timestr, sizeof(timestr));
		print_long_line(path, path, &st, args->human_readable, 1, 1, 1,
				1, user, group, timestr);
		return 0;
	}

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return app_fail_errno("open failed");

	if (fstat(fd, &st)) {
		close(fd);
		return app_fail_errno("stat failed");
	}

	if (S_ISDIR(st.st_mode)) {
		ret = ls_dir(fd, path, args);
	} else {
		puts(path);
	}
	close(fd);
	return ret;
}

static void print_help(void)
{
	printf("Usage: %s [OPTION]... [FILE]...\n", app_progname());
	printf("List directory contents.\n\n");
	printf("  -a, --all                  do not ignore entries starting with .\n");
	printf("  -h, --human-readable       with -l, print sizes in powers of 1024 (e.g. 1K 234M)\n");
	printf("  -l                         long format: mode, links, owner, group, size, mtime, name\n");
	printf("      --help                 display this help and exit\n");
}

static const struct app_option ls_opts[] = {
	{ 'a', 'a', "all", APP_OPT_NO_ARG },
	{ 'l', 'l', NULL, APP_OPT_NO_ARG },
	{ 'h', 'h', "human-readable", APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static int parse_args(struct app_optctx *ctx, struct ls_args *a)
{
	int opt;

	memset(a, 0, sizeof(*a));
	while ((opt = app_optparse(ctx, ls_opts)) != 0) {
		switch (opt) {
		case '?':
		case ':':
			return APP_EXIT_USAGE;
		case APP_OPT_HELP:
			print_help();
			exit(APP_EXIT_OK);
		case 'a':
			a->all = true;
			break;
		case 'l':
			a->long_format = true;
			break;
		case 'h':
			a->human_readable = true;
			break;
		}
	}

	if (app_operand_count(ctx) > 256)
		return app_fail("too many paths");

	return APP_EXIT_OK;
}

int main(int argc, char *argv[])
{
	struct ls_args opt;
	struct app_optctx ctx;
	int err;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);
	err = parse_args(&ctx, &opt);
	if (err)
		return err;

	if (app_operand_count(&ctx) == 0)
		return ls_one(".", &opt);

	if (app_operand_count(&ctx) == 1)
		return ls_one(app_operand(&ctx, 0), &opt);

	bool has_error = false;

	for (int j = 0; j < app_operand_count(&ctx); j++) {
		const char *p = app_operand(&ctx, j);

		printf("%s:\n", p);
		err = ls_one(p, &opt);
		if (err)
			has_error = true;
	}

	return has_error ? APP_EXIT_USAGE : APP_EXIT_OK;
}
