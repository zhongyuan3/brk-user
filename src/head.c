#include <apputil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char usage[] =
	"Usage: head [-n N] [FILE...]\n"
	"Print the first N lines of each FILE (default 10).";

static const struct app_option opts[] = {
	{ 'n', 'n', NULL, APP_OPT_REQUIRED_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static int head_file(FILE *f, unsigned long n)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	while (n > 0 && (len = getline(&line, &cap, f)) != -1) {
		if (fwrite(line, 1, (size_t)len, stdout) != (size_t)len) {
			free(line);
			return app_fail_errno("write failed");
		}
		n--;
	}
	free(line);
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
		return head_file(stdin, n);

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		const char *path = app_operand(&ctx, i);
		FILE *f = fopen(path, "r");

		if (!f) {
			ret = app_fail_errno("cannot open");
			continue;
		}
		if (app_operand_count(&ctx) > 1)
			printf("==> %s <==\n", path);
		if (head_file(f, n) != APP_EXIT_OK)
			ret = APP_EXIT_FAIL;
		fclose(f);
	}

	return ret;
}
