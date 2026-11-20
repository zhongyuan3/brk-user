#include <libcmdfw.h>
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

static const struct cmdfw_option opts[] = {
	{ 'p', 'p', "parents", CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

static int make_parents(const char *path)
{
	char tmp[PATH_MAX];
	int ret = 0;

	if (strlen(path) >= sizeof(tmp)) {
		cmdfw_error("path too long");
		return -1;
	}
	strcpy(tmp, path);

	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
			cmdfw_error_errno("cannot create directory");
			ret = -1;
			break;
		}
		*p = '/';
	}

	if (ret == 0 && mkdir(path, 0755) != 0 && errno != EEXIST) {
		cmdfw_error_errno("cannot create directory");
		ret = -1;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	bool parents = false;
	int opt;
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'p')
			parents = true;
	}

	if (cmdfw_operand_count(&ctx) == 0)
		cmdfw_usage(usage);

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		const char *path = cmdfw_operand(&ctx, i);
		int err;

		if (parents)
			err = make_parents(path);
		else if (mkdir(path, 0755) != 0) {
			cmdfw_error_errno("cannot create directory");
			err = -1;
		} else {
			err = 0;
		}

		if (err)
			ret = CMDFW_EXIT_FAIL;
	}

	return ret;
}
