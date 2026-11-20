#include <libcmdfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: tail [-n N] [FILE...]\n"
			    "Print the last N lines of each FILE (default 10).";

static const struct cmdfw_option opts[] = {
	{ 'n', 'n', NULL, CMDFW_OPT_REQUIRED_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
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
			return cmdfw_fail_errno("out of memory");
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
			return cmdfw_fail_errno("write failed");
		}
	}

	for (unsigned long i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return CMDFW_EXIT_OK;
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	unsigned long n = 10;
	int opt;
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'n') {
			if (cmdfw_parse_ulong(ctx.arg, &n, 0, 1000000) != 0)
				return CMDFW_EXIT_FAIL;
		}
	}

	if (cmdfw_operand_count(&ctx) == 0)
		return tail_file(stdin, n);

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		const char *path = cmdfw_operand(&ctx, i);
		FILE *f = fopen(path, "r");

		if (!f) {
			ret = cmdfw_fail_errno("cannot open");
			continue;
		}
		if (cmdfw_operand_count(&ctx) > 1)
			printf("==> %s <==\n", path);
		if (tail_file(f, n) != CMDFW_EXIT_OK)
			ret = CMDFW_EXIT_FAIL;
		fclose(f);
	}

	return ret;
}
