#include <libcmdfw.h>
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

static const struct cmdfw_option opts[] = {
	{ 'a', 'a', "all", CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
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
	struct cmdfw_optctx ctx;
	int all = 0;
	int opt;
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'a')
			all = 1;
	}

	if (cmdfw_operand_count(&ctx) == 0)
		cmdfw_usage(usage);

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		const char *name = cmdfw_operand(&ctx, i);

		if (strchr(name, '/')) {
			if (is_executable(name))
				puts(name);
			else
				ret = CMDFW_EXIT_FAIL;
		} else if (!search_path(name, all)) {
			ret = CMDFW_EXIT_FAIL;
		}
	}

	return ret;
}
