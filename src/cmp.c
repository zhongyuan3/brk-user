#include <apputil.h>
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

static const struct app_option opts[] = {
	{ 's', 's', NULL, APP_OPT_NO_ARG },
	{ 'l', 'l', NULL, APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
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
	struct app_optctx ctx;
	bool silent = false;
	bool list = false;
	int opt;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(help);
		if (opt == 's')
			silent = true;
		if (opt == 'l')
			list = true;
	}

	app_require_operand_count(&ctx, 2, help);
	if (app_operand_count(&ctx) != 2)
		app_usage(help);

	const char *name1 = app_operand(&ctx, 0);
	const char *name2 = app_operand(&ctx, 1);

	if (!strcmp(name1, "-") && !strcmp(name2, "-"))
		return app_fail("both inputs cannot be standard input");

	int fd1 = !strcmp(name1, "-") ? STDIN_FILENO : open(name1, O_RDONLY);
	int fd2 = !strcmp(name2, "-") ? STDIN_FILENO : open(name2, O_RDONLY);

	if (fd1 < 0)
		return app_fail_errno(name1);
	if (fd2 < 0) {
		if (fd1 != STDIN_FILENO)
			close(fd1);
		return app_fail_errno(name2);
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
			app_error_errno(n1 < 0 ? name1 : name2);
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
	return differ ? APP_EXIT_FAIL : APP_EXIT_OK;
}
