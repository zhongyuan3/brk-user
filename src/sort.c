#include <apputil.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char help[] = "Usage: sort [-nru] [FILE...]\n"
			   "Sort lines from FILE(s), or standard input.\n"
			   "\n"
			   "Options:\n"
			   "  -n       compare leading numbers numerically\n"
			   "  -r       reverse the result\n"
			   "  -u       print only unique lines\n"
			   "  --help   display this help and exit";

static bool numeric;
static bool reverse_order;

static int compare_lines(const void *a, const void *b)
{
	const char *sa = *(const char *const *)a;
	const char *sb = *(const char *const *)b;
	int result;

	if (numeric) {
		char *ea;
		char *eb;
		long long na = strtoll(sa, &ea, 10);
		long long nb = strtoll(sb, &eb, 10);

		(void)ea;
		(void)eb;
		if (na < nb)
			result = -1;
		else if (na > nb)
			result = 1;
		else
			result = strcmp(sa, sb);
	} else {
		result = strcmp(sa, sb);
	}

	return reverse_order ? -result : result;
}

static int lines_add(char ***lines, size_t *nr, size_t *cap, char *line)
{
	if (*nr == *cap) {
		size_t newcap = *cap ? *cap * 2 : 64;
		char **newlines = realloc(*lines, newcap * sizeof(*newlines));

		if (!newlines)
			return -1;
		*lines = newlines;
		*cap = newcap;
	}
	(*lines)[(*nr)++] = line;
	return 0;
}

static int read_lines(FILE *f, char ***lines, size_t *nr, size_t *cap)
{
	char *line = NULL;
	size_t linecap = 0;
	ssize_t len;

	while ((len = getline(&line, &linecap, f)) != -1) {
		while (len > 0 &&
		       (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		char *copy = strdup(line);

		if (!copy) {
			free(line);
			return -1;
		}
		if (lines_add(lines, nr, cap, copy) != 0) {
			free(copy);
			free(line);
			return -1;
		}
	}
	free(line);
	return ferror(f) ? -1 : 0;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	char **lines = NULL;
	size_t nr = 0;
	size_t cap = 0;
	bool unique = false;
	int opt;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	static const struct app_option opts[] = {
		{ 'n', 'n', NULL, APP_OPT_NO_ARG },
		{ 'r', 'r', NULL, APP_OPT_NO_ARG },
		{ 'u', 'u', NULL, APP_OPT_NO_ARG },
		{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
		APP_OPTION_END,
	};

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(help);
		if (opt == 'n')
			numeric = true;
		if (opt == 'r')
			reverse_order = true;
		if (opt == 'u')
			unique = true;
	}

	if (app_operand_count(&ctx) == 0) {
		if (read_lines(stdin, &lines, &nr, &cap) != 0) {
			free(lines);
			return app_fail_errno("read");
		}
	} else {
		for (int i = 0; i < app_operand_count(&ctx); i++) {
			const char *path = app_operand(&ctx, i);
			FILE *f = !strcmp(path, "-") ? stdin : fopen(path, "r");

			if (!f) {
				ret = app_fail_errno(path);
				continue;
			}
			if (read_lines(f, &lines, &nr, &cap) != 0)
				ret = app_fail_errno(path);
			if (f != stdin)
				fclose(f);
		}
	}

	if (nr > 1)
		qsort(lines, nr, sizeof(*lines), compare_lines);

	for (size_t i = 0; i < nr; i++) {
		if (unique && i > 0 && !strcmp(lines[i], lines[i - 1])) {
			free(lines[i]);
			continue;
		}
		puts(lines[i]);
		free(lines[i]);
	}
	free(lines);
	return ret;
}
