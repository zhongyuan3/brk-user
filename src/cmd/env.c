#include <brk/cmd.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] =
	"Usage: env [-i] [-u NAME]... [NAME=VALUE]... [COMMAND [ARG]...]\n"
	"Run COMMAND with a modified environment, or print the environment.\n"
	"\n"
	"Options:\n"
	"  -i       start with an empty environment\n"
	"  -u NAME  remove NAME from the environment\n"
	"  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 'i', 'i', "ignore-environment", BRK_CMD_OPT_NO_ARG },
	{ 'u', 'u', "unset", BRK_CMD_OPT_REQUIRED_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int set_assignment(const char *assignment)
{
	const char *eq = strchr(assignment, '=');
	char name[256];
	size_t len;

	if (!eq)
		return -1;
	len = (size_t)(eq - assignment);
	if (len == 0 || len >= sizeof(name))
		return -1;
	memcpy(name, assignment, len);
	name[len] = '\0';

	return setenv(name, eq + 1, 1);
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool ignore_env = false;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'i')
			ignore_env = true;
		if (opt == 'u' && unsetenv(ctx.arg) != 0)
			return brk_cmd_fail_errno("unsetenv");
	}

	if (ignore_env)
		(void)clearenv();

	int i = ctx.index;

	while (i < ctx.argc && strchr(argv[i], '=')) {
		if (set_assignment(argv[i]) != 0)
			return brk_cmd_fail(
				"invalid environment assignment: %s", argv[i]);
		i++;
	}

	if (i >= ctx.argc) {
		if (environ) {
			for (int j = 0; environ[j]; j++)
				printf("%s\n", environ[j]);
		}
		return BRK_CMD_EXIT_OK;
	}

	execvp(argv[i], &argv[i]);
	int saved_errno = errno;

	brk_cmd_error_errno(argv[i]);
	return saved_errno == ENOENT ? 127 : 126;
}
