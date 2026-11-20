#include <brk/cmd.h>
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

static const struct brk_cmd_option opts[] = {
	{ 'm', 'm', "mode", BRK_CMD_OPT_REQUIRED_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
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
	struct brk_cmd_optctx ctx;
	mode_t mode = 0666;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'm' && parse_mode(ctx.arg, &mode) != 0)
			brk_cmd_usage(usage);
	}

	if (brk_cmd_operand_count(&ctx) == 0)
		brk_cmd_usage(usage);

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *path = brk_cmd_operand(&ctx, i);

		if (mkfifo(path, mode) != 0) {
			brk_cmd_error("%s: %s", path, strerror(errno));
			ret = BRK_CMD_EXIT_FAIL;
		}
	}

	return ret;
}
