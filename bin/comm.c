#include <brk/cmd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] = "Usage: comm [-123] FILE1 FILE2\n"
			    "Compare two sorted files line by line.\n"
			    "\n"
			    "Options:\n"
			    "  -1       suppress lines unique to FILE1\n"
			    "  -2       suppress lines unique to FILE2\n"
			    "  -3       suppress lines common to both files\n"
			    "  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ '1', '1', NULL, BRK_CMD_OPT_NO_ARG },
	{ '2', '2', NULL, BRK_CMD_OPT_NO_ARG },
	{ '3', '3', NULL, BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static bool next_line(FILE *fp, char **line, size_t *cap)
{
	ssize_t n = getline(line, cap, fp);

	if (n < 0)
		return false;
	while (n > 0 && ((*line)[n - 1] == '\n' || (*line)[n - 1] == '\r'))
		(*line)[--n] = '\0';
	return true;
}

static void emit(int column, const char *line)
{
	for (int i = 0; i < column; i++)
		putchar('\t');
	puts(line);
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool s1 = false, s2 = false, s3 = false;
	char *a = NULL, *b = NULL;
	size_t ca = 0, cb = 0;
	bool ea = false, eb = false;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == '1')
			s1 = true;
		if (opt == '2')
			s2 = true;
		if (opt == '3')
			s3 = true;
	}

	brk_cmd_require_operand_count(&ctx, 2, usage);
	const char *pa = brk_cmd_operand(&ctx, 0);
	const char *pb = brk_cmd_operand(&ctx, 1);
	FILE *fa = !strcmp(pa, "-") ? stdin : fopen(pa, "r");
	FILE *fb = !strcmp(pb, "-") ? stdin : fopen(pb, "r");

	if (!fa)
		return brk_cmd_fail_errno(pa);
	if (!fb) {
		if (fa != stdin)
			fclose(fa);
		return brk_cmd_fail_errno(pb);
	}

	for (;;) {
		if (!ea && !a)
			ea = !next_line(fa, &a, &ca);
		if (!eb && !b)
			eb = !next_line(fb, &b, &cb);
		if (ea && eb)
			break;

		if (ea) {
			if (!s2)
				emit(1, b);
			free(b);
			b = NULL;
			continue;
		}
		if (eb) {
			if (!s1)
				emit(0, a);
			free(a);
			a = NULL;
			continue;
		}

		int cmp = strcmp(a, b);

		if (cmp < 0) {
			if (!s1)
				emit(0, a);
			free(a);
			a = NULL;
		} else if (cmp > 0) {
			if (!s2)
				emit(1, b);
			free(b);
			b = NULL;
		} else {
			if (!s3)
				emit(2, a);
			free(a);
			free(b);
			a = b = NULL;
		}
	}

	free(a);
	free(b);
	if (fa != stdin)
		fclose(fa);
	if (fb != stdin)
		fclose(fb);
	return ret;
}
