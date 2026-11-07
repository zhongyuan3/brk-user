#include <brk/cmd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] =
	"Usage: nl [-b STYLE] [-w WIDTH] [-s SEP] [-v START] [-i INC] [FILE...]\n"
	"Number lines of files.\n"
	"\n"
	"Options:\n"
	"  -b STYLE   a=all, t=non-empty (default), n=none\n"
	"  -w WIDTH   number width (default 6)\n"
	"  -s SEP     separator after number (default TAB)\n"
	"  -v START   starting number (default 1)\n"
	"  -i INC     increment (default 1)\n"
	"  --help     display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 'b', 'b', "body-numbering", BRK_CMD_OPT_REQUIRED_ARG },
	{ 'w', 'w', "number-width", BRK_CMD_OPT_REQUIRED_ARG },
	{ 's', 's', "number-separator", BRK_CMD_OPT_REQUIRED_ARG },
	{ 'v', 'v', "starting-line-number", BRK_CMD_OPT_REQUIRED_ARG },
	{ 'i', 'i', "line-increment", BRK_CMD_OPT_REQUIRED_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static void unescape(char *s)
{
	char *out = s;

	while (*s) {
		if (*s != '\\') {
			*out++ = *s++;
			continue;
		}
		s++;
		switch (*s) {
		case 'n':
			*out++ = '\n';
			s++;
			break;
		case 't':
			*out++ = '\t';
			s++;
			break;
		case 'r':
			*out++ = '\r';
			s++;
			break;
		case '\\':
			*out++ = '\\';
			s++;
			break;
		default:
			*out++ = '\\';
			if (*s)
				*out++ = *s++;
			break;
		}
	}
	*out = '\0';
}

static void process(FILE *fp, char style, int width, const char *sep,
		    long *number, long increment)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t n;

	while ((n = getline(&line, &cap, fp)) >= 0) {
		size_t len = (size_t)n;
		bool nonempty = len > 0 && line[0] != '\n';
		bool print_number = style == 'a' || (style == 't' && nonempty);

		if (print_number) {
			printf("%*ld%s", width, *number, sep);
			*number += increment;
		}
		fwrite(line, 1, len, stdout);
	}
	free(line);
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	char style = 't';
	int width = 6;
	char sepbuf[64];
	const char *sep = NULL;
	long number = 1;
	long increment = 1;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'b') {
			if (!ctx.arg || !ctx.arg[0] ||
			    !strchr("atn", ctx.arg[0]))
				brk_cmd_usage(usage);
			style = ctx.arg[0];
		}
		if (opt == 'w') {
			long v;

			if (brk_cmd_parse_long(ctx.arg, &v, 1, 256) != 0)
				brk_cmd_usage(usage);
			width = (int)v;
		}
		if (opt == 's') {
			strlcpy(sepbuf, ctx.arg, sizeof(sepbuf));
			unescape(sepbuf);
			sep = sepbuf;
		}
		if (opt == 'v' &&
		    brk_cmd_parse_long(ctx.arg, &number, 0, 0x7fffffffL) != 0)
			brk_cmd_usage(usage);
		if (opt == 'i' && brk_cmd_parse_long(ctx.arg, &increment, 1,
						     0x7fffffffL) != 0)
			brk_cmd_usage(usage);
	}

	if (!sep)
		sep = "\t";

	if (brk_cmd_operand_count(&ctx) == 0) {
		process(stdin, style, width, sep, &number, increment);
		return BRK_CMD_EXIT_OK;
	}

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *path = brk_cmd_operand(&ctx, i);
		FILE *fp = !strcmp(path, "-") ? stdin : fopen(path, "r");

		if (!fp) {
			brk_cmd_error_errno(path);
			continue;
		}
		process(fp, style, width, sep, &number, increment);
		if (fp != stdin)
			fclose(fp);
	}

	return BRK_CMD_EXIT_OK;
}
