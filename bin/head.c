#include <libcmdfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char usage[] =
	"Usage: head [-n N] [FILE...]\n"
	"Print the first N lines of each FILE (default 10).";

static const struct cmdfw_option opts[] = {
	{ 'n', 'n', NULL, CMDFW_OPT_REQUIRED_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

static int head_file(FILE *f, unsigned long n)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	while (n > 0 && (len = getline(&line, &cap, f)) != -1) {
		if (fwrite(line, 1, (size_t)len, stdout) != (size_t)len) {
			free(line);
			return cmdfw_fail_errno("write failed");
		}
		n--;
	}
	free(line);
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
		return head_file(stdin, n);

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		const char *path = cmdfw_operand(&ctx, i);
		FILE *f = fopen(path, "r");

		if (!f) {
			ret = cmdfw_fail_errno("cannot open");
			continue;
		}
		if (cmdfw_operand_count(&ctx) > 1)
			printf("==> %s <==\n", path);
		if (head_file(f, n) != CMDFW_EXIT_OK)
			ret = CMDFW_EXIT_FAIL;
		fclose(f);
	}

	return ret;
}
