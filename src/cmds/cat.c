#include <brk/cmd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: cat [-n] [FILE...]\n"
			    "Concatenate FILE(s) to standard output.\n"
			    "\n"
			    "Options:\n"
			    "  -n       number output lines\n"
			    "  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 'n', 'n', NULL, BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static unsigned long line_no = 1;
static int at_line_start = 1;

static int write_all(const char *data, size_t len)
{
	while (len) {
		ssize_t n = write(STDOUT_FILENO, data, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		data += n;
		len -= (size_t)n;
	}
	return 0;
}

static int cat_numbered(const char *buf, size_t len)
{
	char out[8192];
	size_t outlen = 0;

	for (size_t i = 0; i < len; i++) {
		if (at_line_start) {
			char prefix[32];
			int n = snprintf(prefix, sizeof(prefix), "%6lu\t",
					 line_no++);

			if (n < 0 || (size_t)n >= sizeof(prefix))
				return -1;
			if (outlen + (size_t)n > sizeof(out)) {
				if (write_all(out, outlen))
					return -1;
				outlen = 0;
			}
			memcpy(out + outlen, prefix, (size_t)n);
			outlen += (size_t)n;
			at_line_start = 0;
		}
		if (outlen == sizeof(out)) {
			if (write_all(out, outlen))
				return -1;
			outlen = 0;
		}
		out[outlen++] = buf[i];
		if (buf[i] == '\n')
			at_line_start = 1;
	}

	if (outlen && write_all(out, outlen))
		return -1;
	return 0;
}

static int cat(int fd, int number_lines)
{
	char buf[1024];

	for (;;) {
		ssize_t rcnt = read(fd, buf, sizeof(buf));

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			return brk_cmd_fail_errno("read error");
		}
		if (rcnt == 0)
			break;
		if (number_lines) {
			if (cat_numbered(buf, (size_t)rcnt))
				return brk_cmd_fail_errno("write error");
		} else if (write_all(buf, (size_t)rcnt)) {
			return brk_cmd_fail_errno("write error");
		}
	}
	return BRK_CMD_EXIT_OK;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	int number_lines = 0;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'n')
			number_lines = 1;
	}

	if (brk_cmd_operand_count(&ctx) == 0)
		return cat(STDIN_FILENO, number_lines);

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *path = brk_cmd_operand(&ctx, i);
		int fd;

		if (!strcmp(path, "-")) {
			fd = STDIN_FILENO;
		} else {
			fd = open(path, O_RDONLY);
			if (fd < 0) {
				ret = brk_cmd_fail_errno("open failed");
				continue;
			}
		}

		if (cat(fd, number_lines) != BRK_CMD_EXIT_OK)
			ret = BRK_CMD_EXIT_FAIL;

		if (fd != STDIN_FILENO)
			close(fd);
	}

	return ret;
}
