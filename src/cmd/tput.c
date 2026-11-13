#include <brk/cmd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int write_all(const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = write(STDOUT_FILENO, buf, len);

		if (n < 0) {
			if (errno == EINTR || errno == EBUSY)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

static int out_seq(const char *seq)
{
	return write_all(seq, strlen(seq));
}

static int out_fmt1(const char *fmt, unsigned int a)
{
	char buf[64];
	int n = snprintf(buf, sizeof(buf), fmt, a);

	if (n < 0 || (size_t)n >= sizeof(buf))
		return -1;
	return write_all(buf, (size_t)n);
}

static int out_fmt2(const char *fmt, unsigned int a, unsigned int b)
{
	char buf[64];
	int n = snprintf(buf, sizeof(buf), fmt, a, b);

	if (n < 0 || (size_t)n >= sizeof(buf))
		return -1;
	return write_all(buf, (size_t)n);
}

static int parse_uint(const char *s, unsigned int *out)
{
	char *end;
	unsigned long v;

	if (!s || !*s)
		return -1;
	errno = 0;
	v = strtoul(s, &end, 10);
	if (errno || *end != '\0' || v > 65535)
		return -1;
	*out = (unsigned int)v;
	return 0;
}

static int query_size(int columns)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
		unsigned int v = columns ? ws.ws_col : ws.ws_row;

		if (v)
			return (int)v;
	}

	return 0;
}

static void usage(void)
{
	fprintf(stderr,
		"usage: tput [-V] capability [args]\n\n"
		"Common capabilities:\n"
		"  cols, lines          terminal size\n"
		"  clear, init, reset   clear/reset the screen\n"
		"  cup row col          move cursor (0-based)\n"
		"  home, el, ed         cursor home / erase line / erase screen\n"
		"  civis, cnorm         hide / show cursor\n"
		"  bold, dim, rev, sgr0 text attributes\n"
		"  smul, rmul           underline on/off\n"
		"  setaf n, setab n     ANSI foreground/background (0-15)\n"
		"  colors               number of colors (8)\n");
}

static int set_color(int background, const char *arg)
{
	unsigned int n;

	if (parse_uint(arg, &n) != 0 || n > 15) {
		fprintf(stderr, "tput: invalid color: %s\n", arg ? arg : "");
		return 1;
	}

	if (n < 8)
		return out_fmt1(background ? "\033[4%um" : "\033[3%um", n) ==
				       0 ?
			       0 :
			       1;
	return out_fmt1(background ? "\033[10%um" : "\033[9%um", n - 8) == 0 ?
		       0 :
		       1;
}

int main(int argc, char *argv[])
{
	const char *cap;
	unsigned int row, col;
	int n;

	brk_cmd_init(argc, argv);

	if (argc < 2) {
		usage();
		return 1;
	}

	if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		usage();
		return 0;
	}
	if (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version")) {
		puts("tput (brk) 0.1");
		return 0;
	}

	if (brk_cmd_term_check() != 0)
		return 1;

	cap = argv[1];

	if (!strcmp(cap, "cols") || !strcmp(cap, "lines")) {
		int v = query_size(!strcmp(cap, "cols"));

		if (v <= 0) {
			fprintf(stderr,
				"tput: cannot determine terminal size\n");
			return 1;
		}
		printf("%d\n", v);
		return 0;
	}

	if (!strcmp(cap, "colors")) {
		puts("8");
		return 0;
	}

	if (!strcmp(cap, "clear"))
		return out_seq("\033[H\033[2J") == 0 ? 0 : 1;
	if (!strcmp(cap, "init") || !strcmp(cap, "reset"))
		return out_seq("\033[0m\033[H\033[2J") == 0 ? 0 : 1;
	if (!strcmp(cap, "home"))
		return out_seq("\033[H") == 0 ? 0 : 1;
	if (!strcmp(cap, "el"))
		return out_seq("\033[K") == 0 ? 0 : 1;
	if (!strcmp(cap, "ed"))
		return out_seq("\033[J") == 0 ? 0 : 1;
	if (!strcmp(cap, "civis"))
		return out_seq("\033[?25l") == 0 ? 0 : 1;
	if (!strcmp(cap, "cnorm"))
		return out_seq("\033[?25h") == 0 ? 0 : 1;
	if (!strcmp(cap, "bold"))
		return out_seq("\033[1m") == 0 ? 0 : 1;
	if (!strcmp(cap, "dim"))
		return out_seq("\033[2m") == 0 ? 0 : 1;
	if (!strcmp(cap, "smul"))
		return out_seq("\033[4m") == 0 ? 0 : 1;
	if (!strcmp(cap, "rmul"))
		return out_seq("\033[24m") == 0 ? 0 : 1;
	if (!strcmp(cap, "rev"))
		return out_seq("\033[7m") == 0 ? 0 : 1;
	if (!strcmp(cap, "sgr0"))
		return out_seq("\033[0m") == 0 ? 0 : 1;
	if (!strcmp(cap, "sc"))
		return out_seq("\033[s") == 0 ? 0 : 1;
	if (!strcmp(cap, "rc"))
		return out_seq("\033[u") == 0 ? 0 : 1;

	if (!strcmp(cap, "cup")) {
		if (argc < 4 || parse_uint(argv[2], &row) != 0 ||
		    parse_uint(argv[3], &col) != 0) {
			fprintf(stderr,
				"tput: cup needs two numeric arguments\n");
			return 1;
		}
		n = out_fmt2("\033[%u;%uH", row, col);
		return n == 0 ? 0 : 1;
	}

	if (!strcmp(cap, "setaf") || !strcmp(cap, "setab")) {
		if (argc < 3) {
			fprintf(stderr, "tput: %s needs a color argument\n",
				cap);
			return 1;
		}
		return set_color(!strcmp(cap, "setab"), argv[2]);
	}

	fprintf(stderr, "tput: unknown capability: %s\n", cap);
	return 1;
}
