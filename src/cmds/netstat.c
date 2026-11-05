#include <brk/cmd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: netstat\n"
			    "Show active TCP connections from /proc/tcp.";

static const struct brk_cmd_option opts[] = {
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
	}

	if (brk_cmd_operand_count(&ctx) != 0)
		brk_cmd_usage(usage);

	int fd = open("/proc/tcp", O_RDONLY);

	if (fd < 0)
		return brk_cmd_fail_errno("cannot open /proc/tcp");

	printf("Active TCP connections\n");

	char buf[512];
	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return brk_cmd_fail_errno("read /proc/tcp failed");
		}
		if (n == 0)
			break;
		char *p = buf;
		ssize_t left = n;

		while (left > 0) {
			ssize_t w = write(STDOUT_FILENO, p, (size_t)left);

			if (w < 0) {
				if (errno == EINTR)
					continue;
				close(fd);
				return brk_cmd_fail_errno("write failed");
			}
			p += w;
			left -= w;
		}
	}
	close(fd);
	return BRK_CMD_EXIT_OK;
}
