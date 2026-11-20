#include <libcmdfw.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] = "Usage: mkfifo [-m MODE] NAME...\n"
			    "Create named pipes (FIFOs).\n"
			    "\n"
			    "Options:\n"
			    "  -m MODE  set file permission bits (octal)\n"
			    "  --help   display this help and exit";

static const struct cmdfw_option opts[] = {
	{ 'm', 'm', "mode", CMDFW_OPT_REQUIRED_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

static int parse_mode(const char *s, mode_t *out)
{
	char *end;
	unsigned long v;

	if (!s || !s[0])
		return -1;
	v = strtoul(s, &end, 8);
	if (*end || v > 07777)
		return -1;
	*out = (mode_t)v;
	return 0;
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	mode_t mode = 0666;
	int opt;
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'm' && parse_mode(ctx.arg, &mode) != 0)
			cmdfw_usage(usage);
	}

	if (cmdfw_operand_count(&ctx) == 0)
		cmdfw_usage(usage);

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		const char *path = cmdfw_operand(&ctx, i);

		if (mkfifo(path, mode) != 0) {
			cmdfw_error("%s: %s", path, strerror(errno));
			ret = CMDFW_EXIT_FAIL;
		}
	}

	return ret;
}
