#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

static int case_nodes(struct test_ctx *ctx)
{
	struct stat st;

	(void)ctx;
	if (stat("/dev/tty0", &st) != 0 || !S_ISCHR(st.st_mode))
		return test_skip("dev: /dev/tty0 is unavailable");
	if (stat("/dev/virtio_blk0", &st) != 0 || !S_ISBLK(st.st_mode))
		return test_skip("dev: /dev/virtio_blk0 is unavailable");
	return 0;
}

static int case_tty_ioctl(struct test_ctx *ctx)
{
	struct winsize ws;

	(void)ctx;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
		if (errno == ENOTTY)
			return test_skip("dev: stdout is not a tty");
		return test_fail_msg("dev: TIOCGWINSZ failed: %d", errno);
	}
	if (ws.ws_col == 0 || ws.ws_row == 0)
		return test_fail_msg("dev: tty size is zero");
	return 0;
}

static int case_blk_read(struct test_ctx *ctx)
{
	unsigned char buf[512];
	int fd;

	(void)ctx;
	fd = open("/dev/virtio_blk0", O_RDONLY);
	if (fd < 0)
		return test_skip("dev: cannot open /dev/virtio_blk0");
	ssize_t n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n != (ssize_t)sizeof(buf))
		return test_fail_msg("dev: block read returned %ld",
				     (long)n);
	return 0;
}

static const struct test_case dev_cases[] = {
	{ "nodes", case_nodes, 0, 10000 },
	{ "tty_ioctl", case_tty_ioctl, 0, 10000 },
	{ "blk_read", case_blk_read, TEST_REQUIRE_ROOT | TEST_OWN_PGRP, 10000 },
};

const struct test_suite brktest_dev_suite = {
	.name = "dev",
	.description = "devtmpfs nodes and basic device access",
	.cases = dev_cases,
	.nr_cases = sizeof(dev_cases) / sizeof(dev_cases[0]),
};
