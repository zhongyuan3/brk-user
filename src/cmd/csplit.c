#include <brk/cmd.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] =
	"Usage: csplit [-s] [-f PREFIX] [-n DIGITS] FILE PATTERN...\n"
	"Split FILE at line numbers or /literal/ patterns.";

static const struct brk_cmd_option opts[] = {
	{ 's', 's', "silent", BRK_CMD_OPT_NO_ARG },
	{ 'f', 'f', "prefix", BRK_CMD_OPT_REQUIRED_ARG },
	{ 'n', 'n', "digits", BRK_CMD_OPT_REQUIRED_ARG },
	{ 'k', 'k', "keep-files", BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int output_piece(const char *prefix, int digits, int index, char **lines,
			size_t start, size_t end, bool silent)
{
	char name[512];
	int n = snprintf(name, sizeof(name), "%s%0*d", prefix, digits, index);

	if (n < 0 || (size_t)n >= sizeof(name))
		return -1;
	FILE *fp = fopen(name, "w");
	if (!fp)
		return -1;

	unsigned long long bytes = 0;

	for (size_t i = start; i < end; i++) {
		size_t len = strlen(lines[i]);

		if (fwrite(lines[i], 1, len, fp) != len) {
			fclose(fp);
			return -1;
		}
		bytes += len;
	}
	fclose(fp);
	if (!silent)
		printf("%llu\n", bytes);
	return 0;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	const char *prefix = "xx";
	int digits = 2;
	bool silent = false;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 's')
			silent = true;
		if (opt == 'f')
			prefix = ctx.arg;
		if (opt == 'n') {
			long v;

			if (brk_cmd_parse_long(ctx.arg, &v, 1, 8) != 0)
				brk_cmd_usage(usage);
			digits = (int)v;
		}
	}

	if (brk_cmd_operand_count(&ctx) < 2)
		brk_cmd_usage(usage);

	const char *path = brk_cmd_operand(&ctx, 0);
	FILE *fp = !strcmp(path, "-") ? stdin : fopen(path, "r");
	if (!fp)
		return brk_cmd_fail_errno(path);

	char **lines = NULL;
	size_t count = 0, cap = 0;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t n;

	while ((n = getline(&line, &linecap, fp)) >= 0) {
		if (count == cap) {
			size_t ncap = cap ? cap * 2 : 64;
			char **nl = realloc(lines, ncap * sizeof(*nl));

			if (!nl) {
				free(line);
				goto oom;
			}
			lines = nl;
			cap = ncap;
		}
		lines[count] = strdup(line);
		if (!lines[count]) {
			free(line);
			goto oom;
		}
		count++;
	}
	free(line);
	if (fp != stdin)
		fclose(fp);

	size_t current = 0;
	int out_index = 0;

	for (int i = 1; i < brk_cmd_operand_count(&ctx); i++) {
		const char *pat = brk_cmd_operand(&ctx, i);
		size_t split;

		if (pat[0] == '/' && pat[1]) {
			size_t plen = strlen(pat);

			if (plen < 2 || pat[plen - 1] != '/') {
				brk_cmd_error("invalid pattern: %s", pat);
				goto fail;
			}
			char *needle = strndup(pat + 1, plen - 2);

			if (!needle)
				goto oom;
			split = count;
			for (size_t j = current; j < count; j++) {
				if (strstr(lines[j], needle)) {
					split = j;
					break;
				}
			}
			free(needle);
			if (split == count) {
				brk_cmd_error("pattern not found: %s", pat);
				goto fail;
			}
		} else {
			char *end;
			unsigned long v = strtoul(pat, &end, 10);

			if (*end || v == 0 || v > count) {
				brk_cmd_error("invalid line number: %s", pat);
				goto fail;
			}
			split = (size_t)v - 1;
		}

		if (split < current) {
			brk_cmd_error("split point out of order");
			goto fail;
		}
		if (output_piece(prefix, digits, out_index++, lines, current,
				 split, silent) != 0) {
			brk_cmd_error_errno("cannot write output");
			goto fail;
		}
		current = split;
	}

	if (output_piece(prefix, digits, out_index++, lines, current, count,
			 silent) != 0) {
		brk_cmd_error_errno("cannot write output");
		goto fail;
	}

	for (size_t i = 0; i < count; i++)
		free(lines[i]);
	free(lines);
	return BRK_CMD_EXIT_OK;

oom:
	brk_cmd_error("out of memory");
fail:
	for (size_t i = 0; i < count; i++)
		free(lines[i]);
	free(lines);
	return BRK_CMD_EXIT_FAIL;
}
