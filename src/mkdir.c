#include <apputil.h>
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

static const struct app_option opts[] = {
	{ 'p', 'p', "parents", APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static int make_parents(const char *path)
{
	char tmp[PATH_MAX];
	int ret = 0;

	if (strlen(path) >= sizeof(tmp)) {
		app_error("path too long");
		return -1;
	}
	strcpy(tmp, path);

	for (char *p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
			app_error_errno("cannot create directory");
			ret = -1;
			break;
		}
		*p = '/';
	}

	if (ret == 0 && mkdir(path, 0755) != 0 && errno != EEXIST) {
		app_error_errno("cannot create directory");
		ret = -1;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	bool parents = false;
	int opt;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 'p')
			parents = true;
	}

	if (app_operand_count(&ctx) == 0)
		app_usage(usage);

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		const char *path = app_operand(&ctx, i);
		int err;

		if (parents)
			err = make_parents(path);
		else if (mkdir(path, 0755) != 0) {
			app_error_errno("cannot create directory");
			err = -1;
		} else {
			err = 0;
		}

		if (err)
			ret = APP_EXIT_FAIL;
	}

	return ret;
}
