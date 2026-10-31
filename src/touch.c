#include <apputil.h>
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

static const struct app_option opts[] = {
	{ 'a', 'a', NULL, APP_OPT_NO_ARG },
	{ 'c', 'c', NULL, APP_OPT_NO_ARG },
	{ 'm', 'm', NULL, APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static int touch_one(const char *path, bool no_create, bool do_atime,
		     bool do_mtime)
{
	struct timespec times[2];
	struct timespec *tp = NULL;

	if (!no_create) {
		int fd = open(path, O_WRONLY | O_CREAT, 0666);

		if (fd < 0) {
			app_error("%s: %s", path, strerror(errno));
			return -1;
		}
		close(fd);
	} else {
		struct stat st;

		if (stat(path, &st) != 0) {
			if (errno == ENOENT)
				return 0;
			app_error("%s: %s", path, strerror(errno));
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
		app_error("%s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	bool no_create = false;
	bool do_atime = false;
	bool do_mtime = false;
	int opt;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 'a')
			do_atime = true;
		if (opt == 'c')
			no_create = true;
		if (opt == 'm')
			do_mtime = true;
	}

	if (!do_atime && !do_mtime)
		do_atime = do_mtime = true;

	if (app_operand_count(&ctx) == 0)
		app_usage(usage);

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		if (touch_one(app_operand(&ctx, i), no_create, do_atime,
			      do_mtime) != 0)
			ret = APP_EXIT_FAIL;
	}

	return ret;
}
