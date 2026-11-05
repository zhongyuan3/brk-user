#include <brk/cmd.h>
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

static const struct brk_cmd_option opts[] = {
	{ 's', 's', "symbolic", BRK_CMD_OPT_NO_ARG },
	{ 'f', 'f', "force", BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool symbolic = false;
	bool force = false;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 's')
			symbolic = true;
		if (opt == 'f')
			force = true;
	}

	if (brk_cmd_operand_count(&ctx) != 2)
		brk_cmd_usage(usage);

	const char *target = brk_cmd_operand(&ctx, 0);
	const char *linkname = brk_cmd_operand(&ctx, 1);

	if (force)
		unlink(linkname);

	int ret = symbolic ? symlink(target, linkname) : link(target, linkname);

	if (ret != 0)
		return brk_cmd_fail_errno(symbolic ? "symlink failed" :
						     "link failed");

	return BRK_CMD_EXIT_OK;
}
