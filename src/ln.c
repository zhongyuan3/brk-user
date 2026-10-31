#include <apputil.h>
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

static const struct app_option opts[] = {
	{ 's', 's', "symbolic", APP_OPT_NO_ARG },
	{ 'f', 'f', "force", APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	bool symbolic = false;
	bool force = false;
	int opt;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 's')
			symbolic = true;
		if (opt == 'f')
			force = true;
	}

	if (app_operand_count(&ctx) != 2)
		app_usage(usage);

	const char *target = app_operand(&ctx, 0);
	const char *linkname = app_operand(&ctx, 1);

	if (force)
		unlink(linkname);

	int ret = symbolic ? symlink(target, linkname) : link(target, linkname);

	if (ret != 0)
		return app_fail_errno(symbolic ? "symlink failed" :
						 "link failed");

	return APP_EXIT_OK;
}
