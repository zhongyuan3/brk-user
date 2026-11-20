#include <libcmdfw.h>
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

static const struct cmdfw_option opts[] = {
	{ 'a', 'a', "append", CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
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
	struct cmdfw_optctx ctx;
	bool append = false;
	int fds[TEE_MAX_FILES + 1];
	int nfds = 1;
	int opt;
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'a')
			append = true;
	}

	if (cmdfw_operand_count(&ctx) > TEE_MAX_FILES)
		return cmdfw_fail("too many files");

	fds[0] = STDOUT_FILENO;
	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
		int fd = open(cmdfw_operand(&ctx, i), flags, 0644);

		if (fd < 0) {
			ret = cmdfw_fail_errno("cannot open");
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
			ret = cmdfw_fail_errno("read error");
			break;
		}
		if (rcnt == 0)
			break;

		for (int i = 0; i < nfds; i++) {
			if (write_all(fds[i], buf, (size_t)rcnt)) {
				cmdfw_error_errno("write error");
				ret = CMDFW_EXIT_FAIL;
			}
		}
	}

	for (int i = 1; i < nfds; i++)
		close(fds[i]);

	return ret;
}
