#include <libcmdfw.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: ln [-sf] TARGET LINK_NAME\n"
			    "Create a hard link, or a symbolic link with -s.\n"
			    "\n"
			    "Options:\n"
			    "  -s       create a symbolic link\n"
			    "  -f       remove an existing destination first\n"
			    "  --help   display this help and exit";

static const struct cmdfw_option opts[] = {
	{ 's', 's', "symbolic", CMDFW_OPT_NO_ARG },
	{ 'f', 'f', "force", CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	bool symbolic = false;
	bool force = false;
	int opt;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 's')
			symbolic = true;
		if (opt == 'f')
			force = true;
	}

	if (cmdfw_operand_count(&ctx) != 2)
		cmdfw_usage(usage);

	const char *target = cmdfw_operand(&ctx, 0);
	const char *linkname = cmdfw_operand(&ctx, 1);

	if (force)
		unlink(linkname);

	int ret = symbolic ? symlink(target, linkname) : link(target, linkname);

	if (ret != 0)
		return cmdfw_fail_errno(symbolic ? "symlink failed" :
						     "link failed");

	return CMDFW_EXIT_OK;
}
