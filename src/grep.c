#include <apputil.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 4096

static char buf[BUF_SIZE];
static char line_buf[BUF_SIZE];

static const char help[] =
	"Usage: grep [OPTION] PATTERN [FILE]\n"
	"Search lines in FILE (or stdin) for PATTERN.\n"
	"\n"
	"Options:\n"
	"  -n       print line numbers before each matching line\n"
	"  --help   display this help and exit\n"
	"\n"
	"Pattern (subset):\n"
	"  ^        match start of line (only at beginning of pattern)\n"
	"  $        match end of line (only at end of pattern)\n"
	"  .        any single character\n"
	"  *        previous atom (literal or .) zero or more times\n";

static const struct app_option opts[] = {
	{ 'n', 'n', NULL, APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static int atom_match(char atom, char c)
{
	if (atom == '.')
		return 1;
	return atom == c;
}

static int rec_match(const char *eff_pat, const char *s, const char *s_end,
		     int must_end)
{
	if (*eff_pat == '\0')
		return !must_end || s == s_end;

	if (eff_pat[0] == '*')
		return 0;

	if (eff_pat[1] == '*') {
		char atom = eff_pat[0];
		const char *after = eff_pat + 2;
		const char *t = s;

		for (;;) {
			if (rec_match(after, t, s_end, must_end))
				return 1;
			if (t >= s_end || !atom_match(atom, *t))
				break;
			t++;
		}
		return 0;
	}

	if (s >= s_end)
		return 0;
	if (atom_match(*eff_pat, *s))
		return rec_match(eff_pat + 1, s + 1, s_end, must_end);
	return 0;
}

static int line_matches(const char *line, size_t line_len, const char *pattern)
{
	int must_start = 0;
	int must_end = 0;
	char eff[BUF_SIZE];
	const char *p = pattern;
	size_t len;

	if (*p == '^') {
		must_start = 1;
		p++;
	}
	len = strlen(p);
	if (len > 0 && p[len - 1] == '$') {
		must_end = 1;
		len--;
	}
	if (len >= sizeof(eff))
		len = sizeof(eff) - 1;
	memcpy(eff, p, len);
	eff[len] = '\0';

	const char *line_end = line + line_len;

	if (must_start)
		return rec_match(eff, line, line_end, must_end);

	for (size_t i = 0; i <= line_len; i++) {
		if (rec_match(eff, line + i, line_end, must_end))
			return 1;
	}
	return 0;
}

static int grep_file(int fd, const char *pattern, int print_line_no)
{
	int line_pos = 0;
	ssize_t rcnt;
	int line_number = 1;
	int found = 0;

	while (1) {
		rcnt = read(fd, buf, sizeof(buf));
		if (rcnt < 0)
			return app_fail_errno("read error");

		if (rcnt < 1)
			break;

		for (ssize_t i = 0; i < rcnt; i++) {
			if (buf[i] == '\n') {
				line_buf[line_pos] = '\0';

				if (line_matches(line_buf, (size_t)line_pos,
						 pattern)) {
					if (print_line_no)
						printf("%d:%s\n", line_number,
						       line_buf);
					else
						printf("%s\n", line_buf);
					found = 1;
				}

				line_pos = 0;
				line_number++;
			} else if (line_pos < BUF_SIZE - 1) {
				line_buf[line_pos++] = buf[i];
			}
		}
	}

	if (line_pos > 0) {
		line_buf[line_pos] = '\0';
		if (line_matches(line_buf, (size_t)line_pos, pattern)) {
			if (print_line_no)
				printf("%d:%s\n", line_number, line_buf);
			else
				printf("%s\n", line_buf);
			found = 1;
		}
	}

	return found ? APP_EXIT_OK : APP_EXIT_FAIL;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	int opt;
	int print_line_no = 0;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(help);
		if (opt == 'n')
			print_line_no = 1;
	}

	if (app_operand_count(&ctx) < 1) {
		app_error("missing pattern");
		fputs("Try 'grep --help' for more information.\n", stderr);
		return APP_EXIT_USAGE;
	}
	if (app_operand_count(&ctx) > 2)
		return app_fail("too many arguments");

	const char *pattern = app_operand(&ctx, 0);
	const char *path = app_operand_count(&ctx) > 1 ? app_operand(&ctx, 1) :
							 NULL;

	if (!path)
		return grep_file(STDIN_FILENO, pattern, print_line_no);

	int fd = open(path, O_RDONLY);

	if (fd < 0)
		return app_fail_errno("open failed");

	int ret = grep_file(fd, pattern, print_line_no);

	close(fd);
	return ret;
}
