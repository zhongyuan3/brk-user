#include <libcmdfw.h>
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

static const struct cmdfw_option opts[] = {
	{ 'b', 'b', "body-numbering", CMDFW_OPT_REQUIRED_ARG },
	{ 'w', 'w', "number-width", CMDFW_OPT_REQUIRED_ARG },
	{ 's', 's', "number-separator", CMDFW_OPT_REQUIRED_ARG },
	{ 'v', 'v', "starting-line-number", CMDFW_OPT_REQUIRED_ARG },
	{ 'i', 'i', "line-increment", CMDFW_OPT_REQUIRED_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
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
	struct cmdfw_optctx ctx;
	char style = 't';
	int width = 6;
	char sepbuf[64];
	const char *sep = NULL;
	long number = 1;
	long increment = 1;
	int opt;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'b') {
			if (!ctx.arg || !ctx.arg[0] ||
			    !strchr("atn", ctx.arg[0]))
				cmdfw_usage(usage);
			style = ctx.arg[0];
		}
		if (opt == 'w') {
			long v;

			if (cmdfw_parse_long(ctx.arg, &v, 1, 256) != 0)
				cmdfw_usage(usage);
			width = (int)v;
		}
		if (opt == 's') {
			strlcpy(sepbuf, ctx.arg, sizeof(sepbuf));
			unescape(sepbuf);
			sep = sepbuf;
		}
		if (opt == 'v' &&
		    cmdfw_parse_long(ctx.arg, &number, 0, 0x7fffffffL) != 0)
			cmdfw_usage(usage);
		if (opt == 'i' && cmdfw_parse_long(ctx.arg, &increment, 1,
						     0x7fffffffL) != 0)
			cmdfw_usage(usage);
	}

	if (!sep)
		sep = "\t";

	if (cmdfw_operand_count(&ctx) == 0) {
		process(stdin, style, width, sep, &number, increment);
		return CMDFW_EXIT_OK;
	}

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		const char *path = cmdfw_operand(&ctx, i);
		FILE *fp = !strcmp(path, "-") ? stdin : fopen(path, "r");

		if (!fp) {
			cmdfw_error_errno(path);
			continue;
		}
		process(fp, style, width, sep, &number, increment);
		if (fp != stdin)
			fclose(fp);
	}

	return CMDFW_EXIT_OK;
}
