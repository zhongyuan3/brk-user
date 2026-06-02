#include <apputil.h>
#include <fcntl.h>
#include <unistd.h>

static char buf[1024];

static int cat(int fd)
{
	ssize_t rcnt, wcnt;

	while (1) {
		rcnt = read(fd, buf, sizeof(buf));
		if (rcnt < 0)
			return app_fail_errno("read error");
		if (rcnt < 1)
			break;
		wcnt = write(STDOUT_FILENO, buf, rcnt);
		if (wcnt < 0)
			return app_fail_errno("write error");
	}

	return APP_EXIT_OK;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	if (app_operand_count(&ctx) == 0)
		return cat(STDIN_FILENO);

	if (app_operand_count(&ctx) != 1)
		app_usage("Usage: cat [FILE]");

	int fd = open(app_operand(&ctx, 0), O_RDONLY);

	if (fd < 0)
		return app_fail_errno("open failed");

	int ret = cat(fd);

	close(fd);
	return ret;
}
