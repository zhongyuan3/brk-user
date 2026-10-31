#include <apputil.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <unistd.h>

#define TEE_MAX_FILES 16

static const char usage[] =
	"Usage: tee [-a] [FILE...]\n"
	"Copy standard input to standard output and FILE(s).\n"
	"\n"
	"Options:\n"
	"  -a       append to files instead of overwriting\n"
	"  --help   display this help and exit";

static const struct app_option opts[] = {
	{ 'a', 'a', "append", APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static int write_all(int fd, const char *buf, size_t len)
{
	while (len) {
		ssize_t n = write(fd, buf, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0) {
			errno = EIO;
			return -1;
		}
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	bool append = false;
	int fds[TEE_MAX_FILES + 1];
	int nfds = 1;
	int opt;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 'a')
			append = true;
	}

	if (app_operand_count(&ctx) > TEE_MAX_FILES)
		return app_fail("too many files");

	fds[0] = STDOUT_FILENO;
	for (int i = 0; i < app_operand_count(&ctx); i++) {
		int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
		int fd = open(app_operand(&ctx, i), flags, 0644);

		if (fd < 0) {
			ret = app_fail_errno("cannot open");
			continue;
		}
		fds[nfds++] = fd;
	}

	char buf[4096];
	for (;;) {
		ssize_t rcnt = read(STDIN_FILENO, buf, sizeof(buf));

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			ret = app_fail_errno("read error");
			break;
		}
		if (rcnt == 0)
			break;

		for (int i = 0; i < nfds; i++) {
			if (write_all(fds[i], buf, (size_t)rcnt)) {
				app_error_errno("write error");
				ret = APP_EXIT_FAIL;
			}
		}
	}

	for (int i = 1; i < nfds; i++)
		close(fds[i]);

	return ret;
}
