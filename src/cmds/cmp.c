#include <brk/cmd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char help[] =
	"Usage: cmp [-s] [-l] FILE1 FILE2\n"
	"Compare two files byte by byte.\n"
	"\n"
	"Options:\n"
	"  -s       print nothing; only set the exit status\n"
	"  -l       print every differing byte\n"
	"  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 's', 's', NULL, BRK_CMD_OPT_NO_ARG },
	{ 'l', 'l', NULL, BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static ssize_t read_chunk(int fd, unsigned char *buf, size_t cap)
{
	size_t used = 0;

	while (used < cap) {
		ssize_t n = read(fd, buf + used, cap - used);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break;
		used += (size_t)n;
	}
	return (ssize_t)used;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool silent = false;
	bool list = false;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(help);
		if (opt == 's')
			silent = true;
		if (opt == 'l')
			list = true;
	}

	brk_cmd_require_operand_count(&ctx, 2, help);
	if (brk_cmd_operand_count(&ctx) != 2)
		brk_cmd_usage(help);

	const char *name1 = brk_cmd_operand(&ctx, 0);
	const char *name2 = brk_cmd_operand(&ctx, 1);

	if (!strcmp(name1, "-") && !strcmp(name2, "-"))
		return brk_cmd_fail("both inputs cannot be standard input");

	int fd1 = !strcmp(name1, "-") ? STDIN_FILENO : open(name1, O_RDONLY);
	int fd2 = !strcmp(name2, "-") ? STDIN_FILENO : open(name2, O_RDONLY);

	if (fd1 < 0)
		return brk_cmd_fail_errno(name1);
	if (fd2 < 0) {
		if (fd1 != STDIN_FILENO)
			close(fd1);
		return brk_cmd_fail_errno(name2);
	}

	unsigned char b1[4096];
	unsigned char b2[4096];
	unsigned long offset = 0;
	unsigned long line = 1;
	bool differ = false;

	for (;;) {
		ssize_t n1 = read_chunk(fd1, b1, sizeof(b1));
		ssize_t n2 = read_chunk(fd2, b2, sizeof(b2));

		if (n1 < 0 || n2 < 0) {
			brk_cmd_error_errno(n1 < 0 ? name1 : name2);
			differ = true;
			break;
		}

		ssize_t common = n1 < n2 ? n1 : n2;

		for (ssize_t i = 0; i < common; i++) {
			if (b1[i] != b2[i]) {
				differ = true;
				if (silent) {
					goto done;
				}
				if (list) {
					printf("%6lu %3o %3o\n", offset + i + 1,
					       b1[i], b2[i]);
				} else {
					printf("%s %s differ: byte %lu, line %lu\n",
					       name1, name2, offset + i + 1,
					       line);
					goto done;
				}
			}
			if (b1[i] == '\n')
				line++;
		}

		if (n1 != n2) {
			if (!silent && !list)
				printf("cmp: EOF on %s after byte %lu\n",
				       n1 < n2 ? name1 : name2,
				       offset + (unsigned long)common);
			differ = true;
			break;
		}
		if (n1 == 0)
			break;
		offset += (unsigned long)common;
	}

done:
	if (fd1 != STDIN_FILENO)
		close(fd1);
	if (fd2 != STDIN_FILENO)
		close(fd2);
	return differ ? BRK_CMD_EXIT_FAIL : BRK_CMD_EXIT_OK;
}
