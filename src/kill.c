#include <apputil.h>
#include <signal.h>
#include <unistd.h>

static const char usage[] = "Usage: kill [-SIGNUM] PID...\n"
			    "  Send signal SIGNUM (default: TERM) to each PID.";

static const struct app_option opts[] = {
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	int opt;
	int sig = SIGTERM;
	int first = 0;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
	}

	app_require_operands(&ctx, 1, usage);

	if (app_operand(&ctx, 0)[0] == '-') {
		long n;

		if (app_parse_long(app_operand(&ctx, 0) + 1, &n, 1, NSIG - 1) !=
		    0)
			return APP_EXIT_FAIL;
		sig = (int)n;
		first = 1;
	}

	if (app_operand_count(&ctx) - first < 1)
		app_usage(usage);

	for (int i = first; i < app_operand_count(&ctx); i++) {
		pid_t pid;

		if (app_parse_pid(app_operand(&ctx, i), &pid) != 0)
			return APP_EXIT_FAIL;

		if (kill(pid, sig) != 0)
			return app_fail_errno("kill failed");
	}

	return APP_EXIT_OK;
}
