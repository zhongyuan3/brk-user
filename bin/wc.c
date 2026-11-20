#include <libcmdfw.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: wc [-lwc] [FILE...]\n"
			    "Count lines, words and bytes.\n"
			    "\n"
			    "Options:\n"
			    "  -l       count lines\n"
			    "  -w       count words\n"
			    "  -c       count bytes\n"
			    "  --help   display this help and exit";

static const struct cmdfw_option opts[] = {
	{ 'l', 'l', NULL, CMDFW_OPT_NO_ARG },
	{ 'w', 'w', NULL, CMDFW_OPT_NO_ARG },
	{ 'c', 'c', NULL, CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

struct wc_count {
	unsigned long lines;
	unsigned long words;
	unsigned long bytes;
};

static int count_fd(int fd, struct wc_count *c)
{
	char buf[1024];
	bool word = false;

	memset(c, 0, sizeof(*c));

	for (;;) {
		ssize_t rcnt = read(fd, buf, sizeof(buf));

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			return cmdfw_fail_errno("read error");
		}
		if (rcnt == 0)
			break;

		for (ssize_t i = 0; i < rcnt; i++) {
			c->bytes++;
			if (buf[i] == '\n')
				c->lines++;
			if (strchr(" \r\t\n\v\f", buf[i])) {
				word = false;
			} else if (!word) {
				c->words++;
				word = true;
			}
		}
	}
	return CMDFW_EXIT_OK;
}

static void print_count(const struct wc_count *c, bool l, bool w, bool b,
			const char *name)
{
	bool first = true;

	if (l) {
		printf("%lu", c->lines);
		first = false;
	}
	if (w) {
		printf("%s%lu", first ? "" : " ", c->words);
		first = false;
	}
	if (b) {
		printf("%s%lu", first ? "" : " ", c->bytes);
		first = false;
	}
	if (name && name[0]) {
		if (!first)
			putchar(' ');
		fputs(name, stdout);
	}
	putchar('\n');
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	bool show_l = false, show_w = false, show_c = false;
	struct wc_count total = { 0, 0, 0 };
	int opt;
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'l')
			show_l = true;
		if (opt == 'w')
			show_w = true;
		if (opt == 'c')
			show_c = true;
	}

	if (!show_l && !show_w && !show_c)
		show_l = show_w = show_c = true;

	if (cmdfw_operand_count(&ctx) == 0) {
		struct wc_count c;

		if (count_fd(STDIN_FILENO, &c) != CMDFW_EXIT_OK)
			return CMDFW_EXIT_FAIL;
		print_count(&c, show_l, show_w, show_c, "");
		return CMDFW_EXIT_OK;
	}

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		const char *path = cmdfw_operand(&ctx, i);
		struct wc_count c;
		int fd = open(path, O_RDONLY);

		if (fd < 0) {
			ret = cmdfw_fail_errno("open failed");
			continue;
		}
		if (count_fd(fd, &c) != CMDFW_EXIT_OK) {
			ret = CMDFW_EXIT_FAIL;
		} else {
			print_count(&c, show_l, show_w, show_c, path);
			total.lines += c.lines;
			total.words += c.words;
			total.bytes += c.bytes;
		}
		close(fd);
	}

	if (cmdfw_operand_count(&ctx) > 1)
		print_count(&total, show_l, show_w, show_c, "total");

	return ret;
}
