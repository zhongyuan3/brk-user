#include <apputil.h>
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

static const struct app_option opts[] = {
	{ 'l', 'l', NULL, APP_OPT_NO_ARG },
	{ 'w', 'w', NULL, APP_OPT_NO_ARG },
	{ 'c', 'c', NULL, APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
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
			return app_fail_errno("read error");
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
	return APP_EXIT_OK;
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
	struct app_optctx ctx;
	bool show_l = false, show_w = false, show_c = false;
	struct wc_count total = { 0, 0, 0 };
	int opt;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 'l')
			show_l = true;
		if (opt == 'w')
			show_w = true;
		if (opt == 'c')
			show_c = true;
	}

	if (!show_l && !show_w && !show_c)
		show_l = show_w = show_c = true;

	if (app_operand_count(&ctx) == 0) {
		struct wc_count c;

		if (count_fd(STDIN_FILENO, &c) != APP_EXIT_OK)
			return APP_EXIT_FAIL;
		print_count(&c, show_l, show_w, show_c, "");
		return APP_EXIT_OK;
	}

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		const char *path = app_operand(&ctx, i);
		struct wc_count c;
		int fd = open(path, O_RDONLY);

		if (fd < 0) {
			ret = app_fail_errno("open failed");
			continue;
		}
		if (count_fd(fd, &c) != APP_EXIT_OK) {
			ret = APP_EXIT_FAIL;
		} else {
			print_count(&c, show_l, show_w, show_c, path);
			total.lines += c.lines;
			total.words += c.words;
			total.bytes += c.bytes;
		}
		close(fd);
	}

	if (app_operand_count(&ctx) > 1)
		print_count(&total, show_l, show_w, show_c, "total");

	return ret;
}
