#include <brk/cmd.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] =
	"Usage: mknod [-m MODE] NAME TYPE [MAJOR MINOR]\n"
	"Create a special file.\n"
	"\n"
	"TYPE is one of:\n"
	"  b       block device (requires MAJOR and MINOR)\n"
	"  c       character device (requires MAJOR and MINOR)\n"
	"  p       FIFO (same as mkfifo)\n"
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

static int parse_number(const char *s, unsigned long max, unsigned long *out)
{
	char *end;
	unsigned long v;

	if (!s || !s[0])
		return -1;
	v = strtoul(s, &end, 0);
	if (*end || v > max)
		return -1;
	*out = v;
	return 0;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	mode_t perm = 0666;
	mode_t mode;
	dev_t dev = 0;
	char type;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'm' && parse_mode(ctx.arg, &perm) != 0)
			brk_cmd_usage(usage);
	}

	if (brk_cmd_operand_count(&ctx) < 2)
		brk_cmd_usage(usage);

	const char *path = brk_cmd_operand(&ctx, 0);
	const char *type_s = brk_cmd_operand(&ctx, 1);

	if (!type_s[0] || type_s[1])
		brk_cmd_usage(usage);
	type = type_s[0];

	switch (type) {
	case 'p':
		if (brk_cmd_operand_count(&ctx) != 2)
			brk_cmd_usage(usage);
		mode = S_IFIFO | perm;
		break;
	case 'b':
	case 'c': {
		unsigned long major, minor;

		if (brk_cmd_operand_count(&ctx) != 4)
			brk_cmd_usage(usage);
		if (parse_number(brk_cmd_operand(&ctx, 2), 255, &major) != 0 ||
		    parse_number(brk_cmd_operand(&ctx, 3), 0xfffff, &minor) !=
			    0)
			brk_cmd_usage(usage);

		mode = (type == 'b' ? S_IFBLK : S_IFCHR) | perm;
		dev = (dev_t)((major << 20) | minor);
		if (type == 'b')
			dev |= (dev_t)1 << 28;
		break;
	}
	default:
		brk_cmd_usage(usage);
	}

	if (mknod(path, mode, dev) != 0)
		return brk_cmd_fail("%s: %s", path, strerror(errno));

	return BRK_CMD_EXIT_OK;
}
