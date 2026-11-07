#include <brk/cmd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] =
	"Usage: paste [-s] [-d LIST] [FILE...]\n"
	"Merge lines of files.\n"
	"\n"
	"Options:\n"
	"  -s       paste one file at a time\n"
	"  -d LIST  use characters from LIST as delimiters (default TAB)\n"
	"  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 's', 's', "serial", BRK_CMD_OPT_NO_ARG },
	{ 'd', 'd', "delimiters", BRK_CMD_OPT_REQUIRED_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static char *unescape_delims(const char *s)
{
	size_t len = strlen(s);
	char *out = malloc(len + 1);
	char *p = out;

	if (!out)
		return NULL;

	while (*s) {
		if (*s != '\\') {
			*p++ = *s++;
			continue;
		}
		s++;
		switch (*s) {
		case 't':
			*p++ = '\t';
			s++;
			break;
		case 'n':
			*p++ = '\n';
			s++;
			break;
		case '\\':
			*p++ = '\\';
			s++;
			break;
		case '0':
			*p++ = '\0';
			s++;
			break;
		default:
			*p++ = '\\';
			if (*s)
				*p++ = *s++;
			break;
		}
	}
	*p = '\0';
	return out;
}

static void strip_line(char *line, ssize_t n)
{
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
		line[--n] = '\0';
}

static void emit_delim(const char *delims, size_t *di)
{
	char c = delims[*di];

	if (c)
		putchar(c);
	if (delims[*di + 1])
		(*di)++;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool serial = false;
	char *delims = NULL;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 's')
			serial = true;
		if (opt == 'd') {
			free(delims);
			delims = unescape_delims(ctx.arg);
			if (!delims)
				return BRK_CMD_EXIT_FAIL;
		}
	}

	if (!delims) {
		delims = strdup("\t");
		if (!delims)
			return BRK_CMD_EXIT_FAIL;
	}
	if (!delims[0]) {
		free(delims);
		delims = strdup("\t");
		if (!delims)
			return BRK_CMD_EXIT_FAIL;
	}

	int nfiles = brk_cmd_operand_count(&ctx);

	if (nfiles == 0) {
		/* Treat stdin as the only input. */
		static char *dummy[] = { "-" };
		(void)dummy;
		nfiles = 1;
	}

	FILE **files = calloc((size_t)nfiles, sizeof(*files));
	char **lines = calloc((size_t)nfiles, sizeof(*lines));
	size_t *caps = calloc((size_t)nfiles, sizeof(*caps));
	bool *eof = calloc((size_t)nfiles, sizeof(*eof));

	if (!files || !lines || !caps || !eof) {
		ret = BRK_CMD_EXIT_FAIL;
		goto out;
	}

	for (int i = 0; i < nfiles; i++) {
		const char *path = nfiles == 1 && brk_cmd_operand_count(&ctx) ==
							   0 ?
					   "-" :
					   brk_cmd_operand(&ctx, i);

		if (!strcmp(path, "-")) {
			files[i] = stdin;
		} else {
			files[i] = fopen(path, "r");
			if (!files[i]) {
				ret = brk_cmd_fail_errno(path);
				goto out;
			}
		}
	}

	size_t di = 0;

	if (serial) {
		for (int i = 0; i < nfiles; i++) {
			ssize_t n;
			bool any = false;

			while ((n = getline(&lines[i], &caps[i], files[i])) >=
			       0) {
				strip_line(lines[i], n);
				if (any)
					emit_delim(delims, &di);
				fputs(lines[i], stdout);
				any = true;
			}
			if (any)
				putchar('\n');
		}
	} else {
		for (;;) {
			bool any = false;

			for (int i = 0; i < nfiles; i++) {
				if (eof[i]) {
					if (lines[i])
						lines[i][0] = '\0';
					continue;
				}
				ssize_t n =
					getline(&lines[i], &caps[i], files[i]);
				if (n < 0) {
					eof[i] = true;
					if (lines[i])
						lines[i][0] = '\0';
					continue;
				}
				strip_line(lines[i], n);
				any = true;
			}
			if (!any)
				break;
			for (int i = 0; i < nfiles; i++) {
				if (i)
					emit_delim(delims, &di);
				fputs(lines[i] ? lines[i] : "", stdout);
			}
			putchar('\n');
		}
	}

out:
	if (files) {
		for (int i = 0; i < nfiles; i++) {
			if (files[i] && files[i] != stdin)
				fclose(files[i]);
		}
	}
	if (lines) {
		for (int i = 0; i < nfiles; i++)
			free(lines[i]);
	}
	free(files);
	free(lines);
	free(caps);
	free(eof);
	free(delims);
	return ret;
}
