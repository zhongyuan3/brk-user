#include <libcmdfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] = "Usage: fold [-s] [-w WIDTH] [FILE...]\n"
			    "Wrap input lines to fit a specified width.\n"
			    "\n"
			    "Options:\n"
			    "  -s           break at spaces\n"
			    "  -w WIDTH     use WIDTH columns (default 80)\n"
			    "  --help       display this help and exit";

static const struct cmdfw_option opts[] = {
	{ 's', 's', "spaces", CMDFW_OPT_NO_ARG },
	{ 'w', 'w', "width", CMDFW_OPT_REQUIRED_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

static void fold_line(const char *line, size_t len, int width, bool spaces)
{
	size_t pos = 0;

	while (pos < len) {
		size_t remaining = len - pos;
		size_t take = remaining < (size_t)width ? remaining :
							  (size_t)width;

		if (spaces && take < remaining) {
			size_t cut = take;

			while (cut > 0 && line[pos + cut] != ' ')
				cut--;
			if (cut > 0)
				take = cut;
		}

		fwrite(line + pos, 1, take, stdout);
		putchar('\n');
		pos += take;
		if (spaces) {
			while (pos < len && line[pos] == ' ')
				pos++;
		}
	}
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	int width = 80;
	bool spaces = false;
	int opt;
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 's')
			spaces = true;
		if (opt == 'w') {
			long w;

			if (cmdfw_parse_long(ctx.arg, &w, 1, 4096) != 0)
				cmdfw_usage(usage);
			width = (int)w;
		}
	}

	int files = cmdfw_operand_count(&ctx);

	if (files == 0) {
		char *line = NULL;
		size_t cap = 0;
		ssize_t n;

		while ((n = getline(&line, &cap, stdin)) >= 0) {
			size_t len = (size_t)n;

			while (len &&
			       (line[len - 1] == '\n' || line[len - 1] == '\r'))
				len--;
			fold_line(line, len, width, spaces);
		}
		free(line);
		return CMDFW_EXIT_OK;
	}

	for (int i = 0; i < files; i++) {
		const char *path = cmdfw_operand(&ctx, i);
		FILE *fp = !strcmp(path, "-") ? stdin : fopen(path, "r");

		if (!fp) {
			ret = cmdfw_fail_errno(path);
			continue;
		}

		char *line = NULL;
		size_t cap = 0;
		ssize_t n;

		while ((n = getline(&line, &cap, fp)) >= 0) {
			size_t len = (size_t)n;

			while (len &&
			       (line[len - 1] == '\n' || line[len - 1] == '\r'))
				len--;
			fold_line(line, len, width, spaces);
		}
		free(line);
		if (fp != stdin)
			fclose(fp);
	}

	return ret;
}
