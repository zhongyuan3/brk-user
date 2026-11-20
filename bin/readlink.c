#include <libcmdfw.h>
#include <brk/limits.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: readlink [-n] PATH...\n"
			    "Print the target of each symbolic link.\n"
			    "\n"
			    "Options:\n"
			    "  -n       do not append a newline\n"
			    "  --help   display this help and exit";

static const struct cmdfw_option opts[] = {
	{ 'n', 'n', NULL, CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	int no_newline = 0;
	int opt;
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'n')
			no_newline = 1;
	}

	if (cmdfw_operand_count(&ctx) == 0)
		cmdfw_usage(usage);

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		const char *path = cmdfw_operand(&ctx, i);
		char buf[PATH_MAX + 1];
		ssize_t n = readlink(path, buf, sizeof(buf) - 1);

		if (n < 0) {
			ret = cmdfw_fail_errno(path);
			continue;
		}
		buf[n] = '\0';
		fputs(buf, stdout);
		if (!no_newline)
			putchar('\n');
	}
	return ret;
}
