#include <apputil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: tail [-n N] [FILE...]\n"
			    "Print the last N lines of each FILE (default 10).";

static const struct app_option opts[] = {
	{ 'n', 'n', NULL, APP_OPT_REQUIRED_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static int tail_file(FILE *f, unsigned long n)
{
	char **lines = NULL;
	char *line = NULL;
	size_t cap = 0;
	unsigned long count = 0;
	ssize_t len;

	if (n) {
		lines = calloc(n, sizeof(*lines));
		if (!lines)
			return app_fail_errno("out of memory");
	}

	while ((len = getline(&line, &cap, f)) != -1) {
		(void)len;
		if (!n) {
			free(line);
			line = NULL;
			cap = 0;
			continue;
		}
		unsigned long slot = count % n;

		free(lines[slot]);
		lines[slot] = line;
		line = NULL;
		cap = 0;
		count++;
	}
	free(line);

	unsigned long start = count > n ? count - n : 0;

	for (unsigned long i = start; i < count; i++) {
		const char *s = lines[i % n];

		if (fputs(s, stdout) < 0) {
			for (unsigned long j = 0; j < n; j++)
				free(lines[j]);
			free(lines);
			return app_fail_errno("write failed");
		}
	}

	for (unsigned long i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return APP_EXIT_OK;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	unsigned long n = 10;
	int opt;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 'n') {
			if (app_parse_ulong(ctx.arg, &n, 0, 1000000) != 0)
				return APP_EXIT_FAIL;
		}
	}

	if (app_operand_count(&ctx) == 0)
		return tail_file(stdin, n);

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		const char *path = app_operand(&ctx, i);
		FILE *f = fopen(path, "r");

		if (!f) {
			ret = app_fail_errno("cannot open");
			continue;
		}
		if (app_operand_count(&ctx) > 1)
			printf("==> %s <==\n", path);
		if (tail_file(f, n) != APP_EXIT_OK)
			ret = APP_EXIT_FAIL;
		fclose(f);
	}

	return ret;
}
