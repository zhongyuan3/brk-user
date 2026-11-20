#include <libcmdfw.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] =
	"Usage: split [-l LINES] [-b BYTES] [-a SUFFIX_LEN] [-d] [FILE [PREFIX]]\n"
	"Split FILE into pieces.\n"
	"\n"
	"Options:\n"
	"  -l LINES       lines per output file (default 1000)\n"
	"  -b BYTES       bytes per output file\n"
	"  -a SUFFIX_LEN  suffix length (default 2)\n"
	"  -d             use numeric suffixes\n"
	"  --help         display this help and exit";

static const struct cmdfw_option opts[] = {
	{ 'l', 'l', "lines", CMDFW_OPT_REQUIRED_ARG },
	{ 'b', 'b', "bytes", CMDFW_OPT_REQUIRED_ARG },
	{ 'a', 'a', "suffix-length", CMDFW_OPT_REQUIRED_ARG },
	{ 'd', 'd', "numeric-suffixes", CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

static int make_name(char *out, size_t size, const char *prefix,
		     unsigned long index, int suffix_len, bool numeric)
{
	if (numeric)
		return snprintf(out, size, "%s%0*lu", prefix, suffix_len,
				index) < (int)size ?
			       0 :
			       -1;

	char suffix[32];

	if (suffix_len <= 0 || suffix_len >= (int)sizeof(suffix))
		return -1;
	for (int i = suffix_len - 1; i >= 0; i--) {
		suffix[i] = (char)('a' + index % 26);
		index /= 26;
	}
	if (index)
		return -1;
	suffix[suffix_len] = '\0';
	return snprintf(out, size, "%s%s", prefix, suffix) < (int)size ? 0 : -1;
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	unsigned long lines = 1000;
	unsigned long bytes = 0;
	int suffix_len = 2;
	bool numeric = false;
	int opt;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'l' &&
		    cmdfw_parse_ulong(ctx.arg, &lines, 1, 0x7fffffffUL) != 0)
			cmdfw_usage(usage);
		if (opt == 'b' &&
		    cmdfw_parse_ulong(ctx.arg, &bytes, 1, 0x7fffffffUL) != 0)
			cmdfw_usage(usage);
		if (opt == 'a') {
			long v;

			if (cmdfw_parse_long(ctx.arg, &v, 1, 8) != 0)
				cmdfw_usage(usage);
			suffix_len = (int)v;
		}
		if (opt == 'd')
			numeric = true;
	}

	const char *file = cmdfw_operand(&ctx, 0);
	const char *prefix = cmdfw_operand(&ctx, 1);
	if (!file)
		file = "-";
	if (!prefix)
		prefix = "x";
	if (cmdfw_operand_count(&ctx) > 2)
		cmdfw_usage(usage);

	FILE *in = !strcmp(file, "-") ? stdin : fopen(file, "r");
	if (!in)
		return cmdfw_fail_errno(file);

	unsigned long index = 0;
	unsigned long count = 0;
	FILE *out = NULL;
	char name[512];
	int ret = CMDFW_EXIT_OK;

	if (bytes) {
		unsigned char *buf = malloc(bytes);

		if (!buf) {
			ret = cmdfw_fail("out of memory");
			goto done;
		}

		for (;;) {
			size_t n = fread(buf, 1, bytes, in);

			if (!n)
				break;
			if (!out) {
				if (make_name(name, sizeof(name), prefix, index,
					      suffix_len, numeric) != 0) {
					ret = cmdfw_fail("suffix exhausted");
					break;
				}
				out = fopen(name, "w");
				if (!out) {
					ret = cmdfw_fail_errno(name);
					break;
				}
				index++;
			}
			if (fwrite(buf, 1, n, out) != n) {
				ret = cmdfw_fail_errno(name);
				break;
			}
			if (n < bytes) {
				fclose(out);
				out = NULL;
				break;
			}
			fclose(out);
			out = NULL;
		}
		free(buf);
	} else {
		char *line = NULL;
		size_t cap = 0;
		ssize_t n;

		while ((n = getline(&line, &cap, in)) >= 0) {
			if (!out) {
				if (make_name(name, sizeof(name), prefix, index,
					      suffix_len, numeric) != 0) {
					ret = cmdfw_fail("suffix exhausted");
					break;
				}
				out = fopen(name, "w");
				if (!out) {
					ret = cmdfw_fail_errno(name);
					break;
				}
				index++;
				count = 0;
			}
			if (fwrite(line, 1, (size_t)n, out) != (size_t)n) {
				ret = cmdfw_fail_errno(name);
				break;
			}
			count++;
			if (count >= lines) {
				fclose(out);
				out = NULL;
			}
		}
		free(line);
	}

done:
	if (out)
		fclose(out);
	if (in != stdin)
		fclose(in);
	return ret;
}
