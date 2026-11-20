#include <brk/cmd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static const char usage[] =
	"Usage: head [-n N] [FILE...]\n"
	"Print the first N lines of each FILE (default 10).";

static const struct brk_cmd_option opts[] = {
	{ 'n', 'n', NULL, BRK_CMD_OPT_REQUIRED_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int head_file(FILE *f, unsigned long n)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	while (n > 0 && (len = getline(&line, &cap, f)) != -1) {
		if (fwrite(line, 1, (size_t)len, stdout) != (size_t)len) {
			free(line);
			return brk_cmd_fail_errno("write failed");
		}
		n--;
	}
	free(line);
	return BRK_CMD_EXIT_OK;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	unsigned long n = 10;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'n') {
			if (brk_cmd_parse_ulong(ctx.arg, &n, 0, 1000000) != 0)
				return BRK_CMD_EXIT_FAIL;
		}
	}

	if (brk_cmd_operand_count(&ctx) == 0)
		return head_file(stdin, n);

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *path = brk_cmd_operand(&ctx, i);
		FILE *f = fopen(path, "r");

		if (!f) {
			ret = brk_cmd_fail_errno("cannot open");
			continue;
		}
		if (brk_cmd_operand_count(&ctx) > 1)
			printf("==> %s <==\n", path);
		if (head_file(f, n) != BRK_CMD_EXIT_OK)
			ret = BRK_CMD_EXIT_FAIL;
		fclose(f);
	}

	return ret;
}
