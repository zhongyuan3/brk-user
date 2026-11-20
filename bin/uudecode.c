#include <libcmdfw.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: uudecode [-o OUTFILE] [FILE]\n"
			    "Decode a uuencoded file.";

static const struct cmdfw_option opts[] = {
	{ 'o', 'o', "output", CMDFW_OPT_REQUIRED_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

static int decode_char(int c)
{
	if (c == '`')
		return 0;
	if (c >= 0x20 && c <= 0x5f)
		return c - 0x20;
	return -1;
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	const char *outname = NULL;
	const char *inname = NULL;
	int opt;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'o')
			outname = ctx.arg;
	}

	if (cmdfw_operand_count(&ctx) > 1)
		cmdfw_usage(usage);
	if (cmdfw_operand_count(&ctx) == 1)
		inname = cmdfw_operand(&ctx, 0);

	FILE *in = !inname || !strcmp(inname, "-") ? stdin : fopen(inname, "r");
	if (!in)
		return cmdfw_fail_errno(inname);

	char *line = NULL;
	size_t cap = 0;
	ssize_t n;
	char name[512];
	bool found = false;
	FILE *out = NULL;
	int ret = CMDFW_EXIT_OK;

	while ((n = getline(&line, &cap, in)) >= 0) {
		if (!strncmp(line, "begin ", 6)) {
			char *p = line + 6;
			char *sp = strchr(p, ' ');

			if (!sp) {
				ret = cmdfw_fail("invalid uuencode header");
				goto out;
			}
			*sp = '\0';
			p = sp + 1;
			size_t len = strlen(p);

			while (len &&
			       (p[len - 1] == '\n' || p[len - 1] == '\r'))
				p[--len] = '\0';
			if (!len || len >= sizeof(name)) {
				ret = cmdfw_fail("invalid uuencode header");
				goto out;
			}
			strcpy(name, p);
			found = true;
			break;
		}
	}

	if (!found) {
		ret = cmdfw_fail("no begin line");
		goto out;
	}

	if (!outname)
		outname = name;
	out = fopen(outname, "w");
	if (!out) {
		ret = cmdfw_fail_errno(outname);
		goto out;
	}

	while ((n = getline(&line, &cap, in)) >= 0) {
		if (!strncmp(line, "end", 3))
			break;
		if (line[0] == '\n' || line[0] == '\r')
			continue;

		int len = decode_char((unsigned char)line[0]);
		if (len < 0) {
			ret = cmdfw_fail("invalid uuencode line");
			goto out;
		}
		unsigned char outbuf[45];
		int oi = 0;

		for (int i = 0; i < len; i += 3) {
			int c1 =
				decode_char((unsigned char)line[1 + i / 3 * 4]);
			int c2 =
				decode_char((unsigned char)line[2 + i / 3 * 4]);
			int c3 =
				decode_char((unsigned char)line[3 + i / 3 * 4]);
			int c4 =
				decode_char((unsigned char)line[4 + i / 3 * 4]);

			if (c1 < 0 || c2 < 0 || c3 < 0 || c4 < 0) {
				ret = cmdfw_fail("invalid uuencode data");
				goto out;
			}
			unsigned int v = ((unsigned int)c1 << 18) |
					 ((unsigned int)c2 << 12) |
					 ((unsigned int)c3 << 6) |
					 (unsigned int)c4;

			if (oi < len)
				outbuf[oi++] = (unsigned char)(v >> 16);
			if (oi < len)
				outbuf[oi++] = (unsigned char)(v >> 8);
			if (oi < len)
				outbuf[oi++] = (unsigned char)v;
		}
		if (fwrite(outbuf, 1, (size_t)oi, out) != (size_t)oi) {
			ret = cmdfw_fail_errno(outname);
			goto out;
		}
	}

out:
	free(line);
	if (out)
		fclose(out);
	if (in != stdin)
		fclose(in);
	return ret;
}
