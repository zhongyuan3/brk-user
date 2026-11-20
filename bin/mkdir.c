#include <brk/cmd.h>
#include <brk/limits.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] = "Usage: mkdir [-p] DIR...\n"
			    "Create directory (or directories).\n"
			    "\n"
			    "Options:\n"
			    "  -p       create parent directories as needed\n"
			    "  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 'p', 'p', "parents", BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int make_parents(const char *path)
{
	char tmp[PATH_MAX];
	int ret = 0;

	if (strlen(path) >= sizeof(tmp)) {
		brk_cmd_error("path too long");
		return -1;
	}
	strcpy(tmp, path);

	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
			brk_cmd_error_errno("cannot create directory");
			ret = -1;
			break;
		}
		*p = '/';
	}

	if (ret == 0 && mkdir(path, 0755) != 0 && errno != EEXIST) {
		brk_cmd_error_errno("cannot create directory");
		ret = -1;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool parents = false;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'p')
			parents = true;
	}

	if (brk_cmd_operand_count(&ctx) == 0)
		brk_cmd_usage(usage);

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *path = brk_cmd_operand(&ctx, i);
		int err;

		if (parents)
			err = make_parents(path);
		else if (mkdir(path, 0755) != 0) {
			brk_cmd_error_errno("cannot create directory");
			err = -1;
		} else {
			err = 0;
		}

		if (err)
			ret = BRK_CMD_EXIT_FAIL;
	}

	return ret;
}
