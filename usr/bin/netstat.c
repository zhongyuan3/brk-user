#include <errno.h>
#include <fcntl.h>
#include <libcmdfw.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: netstat\n"
			    "Show active TCP connections from /proc/tcp.";

static const struct cmdfw_option opts[] = {
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	int opt;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
	}

	if (cmdfw_operand_count(&ctx) != 0)
		cmdfw_usage(usage);

	int fd = open("/proc/tcp", O_RDONLY);

	if (fd < 0)
		return cmdfw_fail_errno("cannot open /proc/tcp");

	printf("Active TCP connections\n");

	char buf[512];
	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return cmdfw_fail_errno("read /proc/tcp failed");
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
				return cmdfw_fail_errno("write failed");
			}
			p += w;
			left -= w;
		}
	}
	close(fd);
	return CMDFW_EXIT_OK;
}
