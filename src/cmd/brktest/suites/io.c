#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int case_pipe_basic(struct test_ctx *ctx)
{
	int p[2];
	char buf[16] = { 0 };

	(void)ctx;
	if (pipe(p) != 0)
		return test_fail_msg("io: pipe failed: %d", errno);
	if (write(p[1], "hello", 5) != 5)
		return test_fail_msg("io: pipe write failed: %d", errno);
	if (read(p[0], buf, sizeof(buf)) != 5 || memcmp(buf, "hello", 5))
		return test_fail_msg("io: pipe read mismatch");
	close(p[1]);
	if (read(p[0], buf, sizeof(buf)) != 0)
		return test_fail_msg("io: pipe EOF mismatch");
	close(p[0]);
	return 0;
}

static int case_readv_writev(struct test_ctx *ctx)
{
	int p[2];
	char a[] = "hello ";
	char b[] = "world";
	char ra[8] = { 0 };
	char rb[8] = { 0 };
	struct iovec wiov[2] = {
		{ .iov_base = a, .iov_len = 6 },
		{ .iov_base = b, .iov_len = 5 },
	};
	struct iovec riov[2] = {
		{ .iov_base = ra, .iov_len = 6 },
		{ .iov_base = rb, .iov_len = 5 },
	};

	(void)ctx;
	if (pipe(p) != 0)
		return test_fail_msg("io: pipe failed");
	if (writev(p[1], wiov, 2) != 11)
		return test_fail_msg("io: writev failed: %d", errno);
	if (readv(p[0], riov, 2) != 11)
		return test_fail_msg("io: readv failed: %d", errno);
	if (memcmp(ra, "hello ", 6) || memcmp(rb, "world", 5))
		return test_fail_msg("io: readv data mismatch");
	close(p[0]);
	close(p[1]);
	return 0;
}

static int case_nonblock_poll(struct test_ctx *ctx)
{
	int p[2];
	char c;
	struct pollfd pfd;

	(void)ctx;
	if (pipe2(p, O_NONBLOCK) != 0)
		return test_fail_msg("io: pipe2 failed: %d", errno);

	if (read(p[0], &c, 1) >= 0 || errno != EAGAIN)
		return test_fail_msg("io: nonblocking read did not return EAGAIN");

	pfd.fd = p[0];
	pfd.events = POLLIN;
	pfd.revents = 0;
	if (poll(&pfd, 1, 0) != 0)
		return test_fail_msg("io: empty pipe poll is not empty");

	if (write(p[1], "x", 1) != 1)
		return test_fail_msg("io: nonblocking write failed");
	if (poll(&pfd, 1, 100) != 1 || !(pfd.revents & POLLIN))
		return test_fail_msg("io: pipe poll did not report POLLIN");
	if (read(p[0], &c, 1) != 1 || c != 'x')
		return test_fail_msg("io: nonblocking read data mismatch");

	close(p[0]);
	close(p[1]);
	return 0;
}

static int case_poll_timeout(struct test_ctx *ctx)
{
	int p[2];
	struct pollfd pfd;
	struct timespec start, end;

	(void)ctx;
	if (pipe(p) != 0)
		return test_fail_msg("io: pipe failed");
	pfd.fd = p[0];
	pfd.events = POLLIN;
	pfd.revents = 0;

	if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
		return test_fail_msg("io: clock_gettime failed");
	if (poll(&pfd, 1, 10) != 0)
		return test_fail_msg("io: poll timeout returned ready");
	if (clock_gettime(CLOCK_MONOTONIC, &end) != 0)
		return test_fail_msg("io: clock_gettime failed");

	long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000L +
			  (end.tv_nsec - start.tv_nsec) / 1000000L;
	if (elapsed_ms < 5)
		return test_fail_msg("io: poll returned too early (%ldms)",
				     elapsed_ms);

	close(p[0]);
	close(p[1]);
	return 0;
}

static int case_select_basic(struct test_ctx *ctx)
{
	int p[2];
	fd_set rfds;
	struct timeval tv;
	char c;

	(void)ctx;
	if (pipe(p) != 0)
		return test_fail_msg("io: pipe failed");
	FD_ZERO(&rfds);
	FD_SET(p[0], &rfds);
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (select(p[0] + 1, &rfds, NULL, NULL, &tv) != 0)
		return test_fail_msg("io: select on empty pipe is ready");

	if (write(p[1], "s", 1) != 1)
		return test_fail_msg("io: select write failed");
	FD_ZERO(&rfds);
	FD_SET(p[0], &rfds);
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (select(p[0] + 1, &rfds, NULL, NULL, &tv) != 1)
		return test_fail_msg("io: select did not report readable");
	if (read(p[0], &c, 1) != 1 || c != 's')
		return test_fail_msg("io: select data mismatch");
	close(p[0]);
	close(p[1]);
	return 0;
}

static int case_sigpipe(struct test_ctx *ctx)
{
	struct sigaction sa = { .sa_handler = SIG_IGN };
	struct sigaction old;
	int p[2];

	(void)ctx;
	if (sigaction(SIGPIPE, &sa, &old) != 0)
		return test_fail_msg("io: sigaction SIGPIPE failed");
	if (pipe(p) != 0)
		return test_fail_msg("io: pipe failed");
	close(p[0]);
	if (write(p[1], "x", 1) >= 0 || errno != EPIPE)
		return test_fail_msg("io: broken pipe write did not return EPIPE");
	close(p[1]);
	(void)sigaction(SIGPIPE, &old, NULL);
	return 0;
}

static int case_large_pipe(struct test_ctx *ctx)
{
	int p[2];
	pid_t pid;
	int status;

	(void)ctx;
	if (pipe(p) != 0)
		return test_fail_msg("io: pipe failed");

	pid = fork();
	if (pid < 0)
		return test_fail_msg("io: fork failed");
	if (pid == 0) {
		unsigned char buf[4096];
		size_t total = 0;

		close(p[1]);
		for (;;) {
			ssize_t n = read(p[0], buf, sizeof(buf));

			if (n < 0) {
				if (errno == EINTR)
					continue;
				_exit(2);
			}
			if (n == 0)
				break;
			for (ssize_t i = 0; i < n; i++) {
				if (buf[i] != (unsigned char)(total + i))
					_exit(3);
			}
			total += (size_t)n;
		}
		close(p[0]);
		_exit(total == 128 * 1024 ? 0 : 4);
	}

	close(p[0]);
	unsigned char buf[4096];
	size_t sent = 0;

	while (sent < 128 * 1024) {
		size_t chunk = sizeof(buf);

		if (chunk > 128 * 1024 - sent)
			chunk = 128 * 1024 - sent;
		for (size_t i = 0; i < chunk; i++)
			buf[i] = (unsigned char)(sent + i);
		ssize_t n = write(p[1], buf, chunk);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return test_fail_msg("io: large pipe write failed: %d",
					     errno);
		}
		sent += (size_t)n;
	}
	close(p[1]);
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return test_fail_msg("io: large pipe reader failed");
	return 0;
}

static const struct test_case io_cases[] = {
	{ "pipe", case_pipe_basic, TEST_OWN_PGRP, 10000 },
	{ "readv_writev", case_readv_writev, TEST_OWN_PGRP, 10000 },
	{ "nonblock_poll", case_nonblock_poll, TEST_OWN_PGRP, 10000 },
	{ "poll_timeout", case_poll_timeout, TEST_OWN_PGRP, 10000 },
	{ "select", case_select_basic, TEST_OWN_PGRP, 10000 },
	{ "sigpipe", case_sigpipe, 0, 10000 },
	{ "large_pipe", case_large_pipe, TEST_OWN_PGRP, 15000 },
};

const struct test_suite brktest_io_suite = {
	.name = "io",
	.description = "pipes, vectors, poll/select and SIGPIPE",
	.cases = io_cases,
	.nr_cases = sizeof(io_cases) / sizeof(io_cases[0]),
};
