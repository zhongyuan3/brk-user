#include <brk/cmd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] = "Usage: expand [-t LIST] [FILE...]\n"
			    "Convert tabs to spaces.\n"
			    "\n"
			    "Options:\n"
			    "  -t LIST  set tab stops (default 8)\n"
			    "  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 't', 't', "tabs", BRK_CMD_OPT_REQUIRED_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

struct tabs {
	int stops[64];
	int count;
	int interval;
};

static int parse_tabs(const char *s, struct tabs *t)
{
	t->count = 0;
	t->interval = 8;
	if (!s)
		return 0;

	while (*s) {
		char *end;
		long v;

		while (*s == ',' || *s == ' ')
			s++;
		if (!*s)
			break;
		v = strtol(s, &end, 0);
		if (end == s || v <= 0 || v > 4096 || t->count >= 64)
			return -1;
		t->stops[t->count++] = (int)v;
		s = end;
	}

	if (t->count == 1)
		t->interval = t->stops[0];
	return 0;
}

static int next_tab(int col, const struct tabs *t)
{
	if (t->count == 0 || t->count == 1)
		return (col / t->interval + 1) * t->interval;
	for (int i = 0; i < t->count; i++) {
		if (t->stops[i] > col)
			return t->stops[i];
	}
	int last = t->stops[t->count - 1];
	int prev = t->count > 1 ? t->stops[t->count - 2] : 0;
	int step = last - prev;

	if (step <= 0)
		step = t->interval;
	return last + ((col - last) / step + 1) * step;
}

static void expand_stream(FILE *fp, const struct tabs *t)
{
	int col = 0;
	int c;

	while ((c = getc(fp)) != EOF) {
		if (c == '\t') {
			int next = next_tab(col, t);

			while (col < next) {
				putchar(' ');
				col++;
			}
		} else if (c == '\n') {
			putchar(c);
			col = 0;
		} else if (c == '\b') {
			putchar(c);
			if (col > 0)
				col--;
		} else {
			putchar(c);
			col++;
		}
	}
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	struct tabs tabs = { 0 };
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 't' && parse_tabs(ctx.arg, &tabs) != 0)
			brk_cmd_usage(usage);
	}

	if (brk_cmd_operand_count(&ctx) == 0) {
		expand_stream(stdin, &tabs);
		return BRK_CMD_EXIT_OK;
	}

	int ret = BRK_CMD_EXIT_OK;
	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *path = brk_cmd_operand(&ctx, i);
		FILE *fp = !strcmp(path, "-") ? stdin : fopen(path, "r");

		if (!fp) {
			ret = brk_cmd_fail_errno(path);
			continue;
		}
		expand_stream(fp, &tabs);
		if (fp != stdin)
			fclose(fp);
	}
	return ret;
}
