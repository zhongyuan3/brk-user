#include <apputil.h>
#include <dirent.h>
#include <fcntl.h>
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

static void print_long_line(const char *name, const struct stat *st,
			    bool human_readable, int wnlink, int wuid, int wgid,
			    int wsize)
{
	char mode[11];
	char szbuf[32];

	mode_string(st->st_mode, mode);
	format_size_field(st->st_size, human_readable, szbuf, sizeof(szbuf));
	printf("%s %*u %*u %*u %*s %s\n", mode, wnlink, st->st_nlink, wuid,
	       st->st_uid, wgid, st->st_gid, wsize, szbuf, name);
}

static int ls_long(const char *path, char **names, size_t n,
		   bool human_readable)
{
	static char path_buf[512];
	size_t path_len = strlen(path);
	struct stat *sts = NULL;
	int wnlink = 1;
	int wuid = 1;
	int wgid = 1;
	int wsize = 1;
	char szbuf[32];

	memcpy(path_buf, path, path_len);
	char *sep;
	if (path_len > 0 && path_buf[path_len - 1] != '/') {
		path_buf[path_len] = '/';
		sep = path_buf + path_len + 1;
	} else {
		sep = path_buf + path_len;
	}
	size_t pb_len = sizeof(path_buf) - (size_t)(sep - path_buf);

	sts = calloc(n, sizeof(*sts));
	if (!sts) {
		return app_fail_errno("calloc failed");
		return 1;
	}

	for (size_t i = 0; i < n; i++) {
		strlcpy(sep, names[i], pb_len);
		if (stat(path_buf, &sts[i]) != 0) {
			free(sts);
			return app_fail_errno("stat failed");
		}
	}

	for (size_t i = 0; i < n; i++) {
		int w;
		const struct stat *st = &sts[i];

		w = uint_field_width(st->st_nlink);
		if (w > wnlink)
			wnlink = w;
		w = uint_field_width(st->st_uid);
		if (w > wuid)
			wuid = w;
		w = uint_field_width(st->st_gid);
		if (w > wgid)
			wgid = w;
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

	for (size_t i = 0; i < n; i++)
		print_long_line(names[i], &sts[i], human_readable, wnlink, wuid,
				wgid, wsize);

	free(sts);
	return 0;
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
	int fd = open(path, O_RDONLY);
	int ret = 0;
	struct stat st;

	if (fd < 0) {
		return app_fail_errno("open failed");
	}

	if (fstat(fd, &st)) {
		return app_fail_errno("stat failed");
	}

	if (S_ISDIR(st.st_mode)) {
		ret = ls_dir(fd, path, args);
		goto done;
	}

	if (args->long_format)
		print_long_line(path, &st, args->human_readable, 1, 1, 1, 1);
	else
		puts(path);

done:
	close(fd);
	return ret;
}

static void print_help(void)
{
	printf("Usage: %s [OPTION]... [FILE]...\n", app_progname());
	printf("List directory contents.\n\n");
	printf("  -a, --all                  do not ignore entries starting with .\n");
	printf("  -h, --human-readable       with -l, print sizes in powers of 1024 (e.g. 1K 234M)\n");
	printf("  -l                         use a long listing format\n");
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
