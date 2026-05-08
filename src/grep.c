#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 4096

static char buf[BUF_SIZE];
static char line_buf[BUF_SIZE];

static void print_help(void)
{
	puts("Usage: grep [OPTION] PATTERN [FILE]");
	puts("Search lines in FILE (or stdin) for PATTERN.");
	puts("");
	puts("Options:");
	puts("  -n       print line numbers before each matching line");
	puts("  --help   display this help and exit");
	puts("");
	puts("Pattern (subset):");
	puts("  ^        match start of line (only at beginning of pattern)");
	puts("  $        match end of line (only at end of pattern)");
	puts("  .        any single character");
	puts("  *        previous atom (literal or .) zero or more times");
}

static int atom_match(char atom, char c)
{
	if (atom == '.')
		return 1;
	return atom == c;
}

/*
 * Match eff_pat against the substring beginning at s; s_end is one past
 * last character. When must_end, a full pattern match must consume up to
 * s_end (line end anchor).
 */
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
	if (len >= sizeof(eff)) {
		len = sizeof(eff) - 1;
	}
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
		if (rcnt < 0) {
			perror("grep: read error");
			return 1;
		}

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
			} else {
				if (line_pos < BUF_SIZE - 1) {
					line_buf[line_pos++] = buf[i];
				}
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

	return found ? 0 : 1;
}

int main(int argc, char *argv[])
{
	int print_line_no = 0;
	int i = 1;

	while (i < argc) {
		if (strcmp(argv[i], "--help") == 0) {
			print_help();
			return 0;
		}
		if (strcmp(argv[i], "-n") == 0) {
			print_line_no = 1;
			i++;
			continue;
		}
		break;
	}

	if (i >= argc) {
		fputs("grep: missing pattern\n", stderr);
		fputs("Try 'grep --help' for more information.\n", stderr);
		return 2;
	}

	const char *pattern = argv[i++];
	const char *path = (i < argc) ? argv[i++] : NULL;

	if (i < argc) {
		fputs("grep: too many arguments\n", stderr);
		return 2;
	}

	if (!path)
		return grep_file(STDIN_FILENO, pattern, print_line_no);

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror("grep: open failed");
		return 1;
	}
	int ret = grep_file(fd, pattern, print_line_no);
	close(fd);
	return ret;
}
