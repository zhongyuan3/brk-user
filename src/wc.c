#include <apputil.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

static char buf[512];

static int wc(int fd, const char *name)
{
	size_t line_cnt = 0;
	size_t word_cnt = 0;
	size_t char_cnt = 0;
	bool word = false;

	while (1) {
		ssize_t rcnt = read(fd, buf, sizeof(buf));

		if (rcnt < 0)
			return app_fail_errno("read error");
		if (rcnt < 1)
			break;
		for (ssize_t i = 0; i < rcnt; i++) {
			char_cnt++;
			if (buf[i] == '\n')
				line_cnt++;
			if (strchr(" \r\t\n\v", buf[i]))
				word = false;
			else if (!word) {
				word_cnt++;
				word = true;
			}
		}
	}

	printf("%lu %lu %lu %s\n", line_cnt, word_cnt, char_cnt, name);
	return APP_EXIT_OK;
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	if (app_operand_count(&ctx) == 0)
		return wc(STDIN_FILENO, "");

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		const char *path = app_operand(&ctx, i);
		int fd = open(path, O_RDONLY);
		int ret;

		if (fd < 0)
			return app_fail_errno("open failed");
		ret = wc(fd, path);
		close(fd);
		if (ret != APP_EXIT_OK)
			return ret;
	}

	return APP_EXIT_OK;
}
