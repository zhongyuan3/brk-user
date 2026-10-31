#include <apputil.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: netstat\n"
			    "Show active TCP connections from /proc/tcp.";

static const struct app_option opts[] = {
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	int opt;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
	}

	if (app_operand_count(&ctx) != 0)
		app_usage(usage);

	int fd = open("/proc/tcp", O_RDONLY);

	if (fd < 0)
		return app_fail_errno("cannot open /proc/tcp");

	printf("Active TCP connections\n");

	char buf[512];
	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			close(fd);
			return app_fail_errno("read /proc/tcp failed");
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
				return app_fail_errno("write failed");
			}
			p += w;
			left -= w;
		}
	}
	close(fd);
	return APP_EXIT_OK;
}
