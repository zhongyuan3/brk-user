#include <apputil.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char help[] =
	"Usage: od [-A BASE] [-t TYPE] [FILE...]\n"
	"Dump FILE(s) in a simple octal/hex/character format.\n"
	"\n"
	"Options:\n"
	"  -A BASE   address base: o (octal), x (hex), d (decimal), n (none)\n"
	"  -t TYPE   output type: x1, o1, u1, or c\n"
	"  --help    display this help and exit";

static const struct app_option opts[] = {
	{ 'A', 'A', NULL, APP_OPT_REQUIRED_ARG },
	{ 't', 't', NULL, APP_OPT_REQUIRED_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static void print_address(unsigned long offset, char base)
{
	switch (base) {
	case 'n':
		break;
	case 'x':
		printf("%07lx ", offset);
		break;
	case 'd':
		printf("%07lu ", offset);
		break;
	case 'o':
	default:
		printf("%07lo ", offset);
		break;
	}
}

static void print_byte(unsigned char c, char type)
{
	switch (type) {
	case 'o':
		printf("%03o ", c);
		break;
	case 'u':
		printf("%3u ", c);
		break;
	case 'c':
		if (c >= 0x20 && c < 0x7f)
			printf("%3c ", c);
		else if (c == '\n')
			printf(" \\n ");
		else if (c == '\t')
			printf(" \\t ");
		else if (c == '\r')
			printf(" \\r ");
		else
			printf("  . ");
		break;
	case 'x':
	default:
		printf("%02x ", c);
		break;
	}
}

static int dump_fd(int fd, unsigned long *offset, char addr_base, char type)
{
	unsigned char buf[16];

	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break;

		print_address(*offset, addr_base);
		for (ssize_t i = 0; i < n; i++)
			print_byte(buf[i], type);
		putchar('\n');
		*offset += (unsigned long)n;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	char addr_base = 'o';
	char type = 'o';
	int opt;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(help);
		if (opt == 'A') {
			if (ctx.arg[0] && strchr("oxdn", ctx.arg[0]) &&
			    !ctx.arg[1])
				addr_base = ctx.arg[0];
			else
				return app_fail("invalid address base '%s'",
						ctx.arg);
		}
		if (opt == 't') {
			if ((ctx.arg[0] == 'x' || ctx.arg[0] == 'o' ||
			     ctx.arg[0] == 'u' || ctx.arg[0] == 'c') &&
			    (!ctx.arg[1] || (ctx.arg[1] == '1' && !ctx.arg[2])))
				type = ctx.arg[0];
			else
				return app_fail("unsupported type '%s'",
						ctx.arg);
		}
	}

	unsigned long offset = 0;

	if (app_operand_count(&ctx) == 0) {
		if (dump_fd(STDIN_FILENO, &offset, addr_base, type) != 0)
			return app_fail_errno("read");
		return APP_EXIT_OK;
	}

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		const char *path = app_operand(&ctx, i);
		int fd = !strcmp(path, "-") ? STDIN_FILENO :
					      open(path, O_RDONLY);

		if (fd < 0) {
			ret = app_fail_errno(path);
			continue;
		}
		if (dump_fd(fd, &offset, addr_base, type) != 0)
			ret = app_fail_errno(path);
		if (fd != STDIN_FILENO)
			close(fd);
	}
	return ret;
}
