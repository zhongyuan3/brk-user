#include <brk/cmd.h>
#include <brk/limits.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: readlink [-n] PATH...\n"
			    "Print the target of each symbolic link.\n"
			    "\n"
			    "Options:\n"
			    "  -n       do not append a newline\n"
			    "  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 'n', 'n', NULL, BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	int no_newline = 0;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'n')
			no_newline = 1;
	}

	if (brk_cmd_operand_count(&ctx) == 0)
		brk_cmd_usage(usage);

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *path = brk_cmd_operand(&ctx, i);
		char buf[PATH_MAX + 1];
		ssize_t n = readlink(path, buf, sizeof(buf) - 1);

		if (n < 0) {
			ret = brk_cmd_fail_errno(path);
			continue;
		}
		buf[n] = '\0';
		fputs(buf, stdout);
		if (!no_newline)
			putchar('\n');
	}
	return ret;
}
