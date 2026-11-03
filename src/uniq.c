#include <apputil.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char help[] =
	"Usage: uniq [-cdu] [FILE]\n"
	"Filter adjacent matching lines from FILE, or standard input.\n"
	"\n"
	"Options:\n"
	"  -c       prefix lines with the count of occurrences\n"
	"  -d       print only lines that occur more than once\n"
	"  -u       print only lines that occur once\n"
	"  --help   display this help and exit";

static const struct app_option opts[] = {
	{ 'c', 'c', NULL, APP_OPT_NO_ARG },
	{ 'd', 'd', NULL, APP_OPT_NO_ARG },
	{ 'u', 'u', NULL, APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static void emit_line(const char *line, unsigned long count, bool show_count,
		      bool only_dup, bool only_unique)
{
	if (only_dup && count < 2)
		return;
	if (only_unique && count != 1)
		return;

	if (show_count)
		printf("%7lu %s\n", count, line);
	else
		puts(line);
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	FILE *f = stdin;
	char *line = NULL;
	size_t cap = 0;
	char *prev = NULL;
	unsigned long count = 0;
	bool show_count = false;
	bool only_dup = false;
	bool only_unique = false;
	int opt;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(help);
		if (opt == 'c')
			show_count = true;
		if (opt == 'd')
			only_dup = true;
		if (opt == 'u')
			only_unique = true;
	}

	if (app_operand_count(&ctx) > 1)
		app_usage(help);
	if (app_operand_count(&ctx) == 1) {
		const char *path = app_operand(&ctx, 0);

		if (strcmp(path, "-")) {
			f = fopen(path, "r");
			if (!f)
				return app_fail_errno(path);
		}
	}

	for (;;) {
		ssize_t len = getline(&line, &cap, f);

		if (len < 0)
			break;
		while (len > 0 &&
		       (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		if (prev && !strcmp(prev, line)) {
			count++;
			continue;
		}

		if (prev) {
			emit_line(prev, count, show_count, only_dup,
				  only_unique);
			free(prev);
		}
		prev = strdup(line);
		if (!prev) {
			ret = app_fail("out of memory");
			break;
		}
		count = 1;
	}

	if (ferror(f)) {
		ret = app_fail_errno("read");
	} else if (prev) {
		emit_line(prev, count, show_count, only_dup, only_unique);
	}

	free(prev);
	free(line);
	if (f != stdin)
		fclose(f);
	return ret;
}
