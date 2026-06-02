#include <apputil.h>
#include <fcntl.h>
#include <unistd.h>

static const char usage[] = "Usage: cp <src> <dest>";

static const struct app_option opts[] = {
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static int cp(const char *src, const char *dest)
{
	char buf[1024];
	int ret = 0;
	int fd_src = open(src, O_RDONLY);

	if (fd_src < 0)
		return app_fail_errno("open failed");

	int fd_dest = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd_dest < 0) {
		close(fd_src);
		return app_fail_errno("open failed");
	}

	while (1) {
		ssize_t rcnt = read(fd_src, buf, sizeof(buf));

		if (rcnt < 0) {
			app_error_errno("read failed");
			ret = APP_EXIT_FAIL;
			break;
		}
		if (rcnt < 1)
			break;
		ssize_t wcnt = write(fd_dest, buf, rcnt);

		if (wcnt < 0) {
			app_error_errno("write failed");
			ret = APP_EXIT_FAIL;
			break;
		}
	}

	close(fd_src);
	close(fd_dest);
	return ret;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	int opt;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
	}

	app_require_operands(&ctx, 2, usage);
	if (app_operand_count(&ctx) > 2)
		return app_fail("extra operand");

	return cp(app_operand(&ctx, 0), app_operand(&ctx, 1));
}
