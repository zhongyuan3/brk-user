#include <libcmdfw.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char usage[] =
	"Usage: touch [-acm] FILE...\n"
	"Update the access and modification times of each FILE.\n"
	"\n"
	"Options:\n"
	"  -a       change only the access time\n"
	"  -c       do not create files that do not exist\n"
	"  -m       change only the modification time\n"
	"  --help   display this help and exit";

static const struct cmdfw_option opts[] = {
	{ 'a', 'a', NULL, CMDFW_OPT_NO_ARG },
	{ 'c', 'c', NULL, CMDFW_OPT_NO_ARG },
	{ 'm', 'm', NULL, CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

static int touch_one(const char *path, bool no_create, bool do_atime,
		     bool do_mtime)
{
	struct timespec times[2];
	struct timespec *tp = NULL;

	if (!no_create) {
		int fd = open(path, O_WRONLY | O_CREAT, 0666);

		if (fd < 0) {
			cmdfw_error("%s: %s", path, strerror(errno));
			return -1;
		}
		close(fd);
	} else {
		struct stat st;

		if (stat(path, &st) != 0) {
			if (errno == ENOENT)
				return 0;
			cmdfw_error("%s: %s", path, strerror(errno));
			return -1;
		}
	}

	if (do_atime && do_mtime) {
		tp = NULL;
	} else {
		times[0].tv_sec = 0;
		times[0].tv_nsec = do_atime ? UTIME_NOW : UTIME_OMIT;
		times[1].tv_sec = 0;
		times[1].tv_nsec = do_mtime ? UTIME_NOW : UTIME_OMIT;
		tp = times;
	}

	if (utimensat(AT_FDCWD, path, tp, 0) != 0) {
		cmdfw_error("%s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	bool no_create = false;
	bool do_atime = false;
	bool do_mtime = false;
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
			do_atime = true;
		if (opt == 'c')
			no_create = true;
		if (opt == 'm')
			do_mtime = true;
	}

	if (!do_atime && !do_mtime)
		do_atime = do_mtime = true;

	if (cmdfw_operand_count(&ctx) == 0)
		cmdfw_usage(usage);

	for (int i = 0; i < cmdfw_operand_count(&ctx); i++) {
		if (touch_one(cmdfw_operand(&ctx, i), no_create, do_atime,
			      do_mtime) != 0)
			ret = CMDFW_EXIT_FAIL;
	}

	return ret;
}
