#include <brk/cmd.h>
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

static const struct brk_cmd_option opts[] = {
	{ 'a', 'a', NULL, BRK_CMD_OPT_NO_ARG },
	{ 'c', 'c', NULL, BRK_CMD_OPT_NO_ARG },
	{ 'm', 'm', NULL, BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int touch_one(const char *path, bool no_create, bool do_atime,
		     bool do_mtime)
{
	struct timespec times[2];
	struct timespec *tp = NULL;

	if (!no_create) {
		int fd = open(path, O_WRONLY | O_CREAT, 0666);

		if (fd < 0) {
			brk_cmd_error("%s: %s", path, strerror(errno));
			return -1;
		}
		close(fd);
	} else {
		struct stat st;

		if (stat(path, &st) != 0) {
			if (errno == ENOENT)
				return 0;
			brk_cmd_error("%s: %s", path, strerror(errno));
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
		brk_cmd_error("%s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool no_create = false;
	bool do_atime = false;
	bool do_mtime = false;
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
			do_atime = true;
		if (opt == 'c')
			no_create = true;
		if (opt == 'm')
			do_mtime = true;
	}

	if (!do_atime && !do_mtime)
		do_atime = do_mtime = true;

	if (brk_cmd_operand_count(&ctx) == 0)
		brk_cmd_usage(usage);

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		if (touch_one(brk_cmd_operand(&ctx, i), no_create, do_atime,
			      do_mtime) != 0)
			ret = BRK_CMD_EXIT_FAIL;
	}

	return ret;
}
