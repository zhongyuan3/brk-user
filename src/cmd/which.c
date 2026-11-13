#include <brk/cmd.h>
#include <brk/limits.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_PATH "/bin:/sbin:/usr/bin:/usr/sbin"

static const char usage[] = "Usage: which [-a] COMMAND...\n"
			    "Locate executable commands in PATH.\n"
			    "\n"
			    "Options:\n"
			    "  -a, --all  print all matches\n"
			    "  --help     display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 'a', 'a', "all", BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int is_executable(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return 0;
	return access(path, X_OK) == 0;
}

/*
 * Return 1 when at least one match was printed.
 */
static int search_path(const char *name, int all)
{
	char path_buf[PATH_MAX];
	const char *path = getenv("PATH");
	size_t name_len = strlen(name);
	int found = 0;

	if (!path)
		path = DEFAULT_PATH;

	if (name_len >= PATH_MAX)
		return 0;

	const char *p = path;

	for (;;) {
		const char *e = strchr(p, ':');
		size_t len;
		size_t start;

		if (!e)
			e = p + strlen(p);
		len = (size_t)(e - p);

		if (len == 0) {
			start = 2;
			if (name_len + start + 1 > PATH_MAX) {
				if (!*e)
					break;
				p = e + 1;
				continue;
			}
			path_buf[0] = '.';
			path_buf[1] = '/';
		} else {
			start = len + 1;
			if (len >= PATH_MAX ||
			    name_len + start + 1 > PATH_MAX) {
				if (!*e)
					break;
				p = e + 1;
				continue;
			}
			memcpy(path_buf, p, len);
			path_buf[len] = '/';
		}

		memcpy(path_buf + start, name, name_len + 1);
		if (is_executable(path_buf)) {
			puts(path_buf);
			found = 1;
			if (!all)
				return 1;
		}

		if (!*e)
			break;
		p = e + 1;
	}

	return found;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	int all = 0;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'a')
			all = 1;
	}

	if (brk_cmd_operand_count(&ctx) == 0)
		brk_cmd_usage(usage);

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *name = brk_cmd_operand(&ctx, i);

		if (strchr(name, '/')) {
			if (is_executable(name))
				puts(name);
			else
				ret = BRK_CMD_EXIT_FAIL;
		} else if (!search_path(name, all)) {
			ret = BRK_CMD_EXIT_FAIL;
		}
	}

	return ret;
}
