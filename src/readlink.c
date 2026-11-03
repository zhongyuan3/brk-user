#include <apputil.h>
#include <brk/limits.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: readlink [-n] PATH...\n"
			    "Print the target of each symbolic link.\n"
			    "\n"
			    "Options:\n"
			    "  -n       do not append a newline\n"
			    "  --help   display this help and exit";

static const struct app_option opts[] = {
	{ 'n', 'n', NULL, APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	int no_newline = 0;
	int opt;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 'n')
			no_newline = 1;
	}

	if (app_operand_count(&ctx) == 0)
		app_usage(usage);

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		const char *path = app_operand(&ctx, i);
		char buf[PATH_MAX + 1];
		ssize_t n = readlink(path, buf, sizeof(buf) - 1);

		if (n < 0) {
			ret = app_fail_errno(path);
			continue;
		}
		buf[n] = '\0';
		fputs(buf, stdout);
		if (!no_newline)
			putchar('\n');
	}
	return ret;
}
