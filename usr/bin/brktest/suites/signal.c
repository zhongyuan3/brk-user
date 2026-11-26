#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t caught;
static volatile sig_atomic_t caught_signo;

static void on_signal(int sig)
{
	caught++;
	caught_signo = sig;
}

static void sleep_ms(long ms)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = ms * 1000000 };

	(void)nanosleep(&ts, NULL);
}

static int install_handler(int sig, unsigned long flags)
{
	struct sigaction sa = {
		.sa_handler = on_signal,
		.sa_mask = 0,
		.sa_flags = flags,
	};

	return sigaction(sig, &sa, NULL);
}

static int case_handler(struct test_ctx *ctx)
{
	(void)ctx;
	caught = 0;
	caught_signo = 0;
	if (install_handler(SIGUSR1, 0) != 0)
		return test_fail_msg("signal: sigaction failed: %d", errno);
	if (kill(getpid(), SIGUSR1) != 0)
		return test_fail_msg("signal: kill failed: %d", errno);
	if (caught != 1 || caught_signo != SIGUSR1)
		return test_fail_msg("signal: handler did not run");
	return 0;
}

static int case_mask(struct test_ctx *ctx)
{
	sigset_t set = (sigset_t)1 << SIGUSR1;
	sigset_t old;
	sigset_t pending;
	int sig = 0;

	(void)ctx;
	caught = 0;
	if (install_handler(SIGUSR1, 0) != 0)
		return test_fail_msg("signal: sigaction failed: %d", errno);
	if (sigprocmask(SIG_BLOCK, &set, &old) != 0)
		return test_fail_msg("signal: sigprocmask failed: %d", errno);
	if (kill(getpid(), SIGUSR1) != 0)
		return test_fail_msg("signal: kill failed: %d", errno);
	if (caught != 0)
		return test_fail_msg("signal: blocked signal was delivered");
	if (sigpending(&pending) != 0 || !(pending & set))
		return test_fail_msg("signal: SIGUSR1 is not pending");
	if (sigwait(&set, &sig) != 0 || sig != SIGUSR1)
		return test_fail_msg("signal: sigwait returned %d", sig);
	if (sigpending(&pending) != 0 || (pending & set))
		return test_fail_msg("signal: sigwait left signal pending");
	if (sigprocmask(SIG_SETMASK, &old, NULL) != 0)
		return test_fail_msg("signal: sigprocmask restore failed");
	return 0;
}

static int case_suspend(struct test_ctx *ctx)
{
	sigset_t block = (sigset_t)1 << SIGUSR1;
	sigset_t empty = 0;
	sigset_t old;
	pid_t pid;

	(void)ctx;
	caught = 0;
	if (install_handler(SIGUSR1, 0) != 0)
		return test_fail_msg("signal: sigaction failed: %d", errno);
	if (sigprocmask(SIG_BLOCK, &block, &old) != 0)
		return test_fail_msg("signal: sigprocmask failed: %d", errno);

	pid = fork();
	if (pid < 0)
		return test_fail_msg("signal: fork failed: %d", errno);
	if (pid == 0) {
		sleep_ms(20);
		_exit(kill(getppid(), SIGUSR1) == 0 ? 0 : 1);
	}

	errno = 0;
	if (sigsuspend(&empty) != -1 || errno != EINTR)
		return test_fail_msg("signal: sigsuspend did not return EINTR");
	if (caught != 1)
		return test_fail_msg("signal: sigsuspend handler did not run");

	int status;

	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return test_fail_msg("signal: sigsuspend child failed");
	if (sigprocmask(SIG_SETMASK, &old, NULL) != 0)
		return test_fail_msg("signal: sigprocmask restore failed");
	return 0;
}

static int case_interrupt(struct test_ctx *ctx)
{
	struct timespec req = { .tv_sec = 1, .tv_nsec = 0 };
	struct timespec rem = { 0, 0 };
	pid_t pid;

	(void)ctx;
	caught = 0;
	if (install_handler(SIGUSR1, 0) != 0)
		return test_fail_msg("signal: sigaction failed: %d", errno);

	pid = fork();
	if (pid < 0)
		return test_fail_msg("signal: fork failed: %d", errno);
	if (pid == 0) {
		sleep_ms(20);
		_exit(kill(getppid(), SIGUSR1) == 0 ? 0 : 1);
	}

	errno = 0;
	if (nanosleep(&req, &rem) != -1 || errno != EINTR)
		return test_fail_msg("signal: nanosleep was not interrupted");
	if (caught != 1)
		return test_fail_msg("signal: interrupted handler did not run");
	if (rem.tv_sec == 0 && rem.tv_nsec == 0)
		return test_fail_msg("signal: nanosleep remaining time is zero");

	int status;

	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return test_fail_msg("signal: interrupt child failed");
	return 0;
}

static int case_read_eintr(struct test_ctx *ctx)
{
	int pipefd[2];
	char c = 0;
	pid_t pid;

	(void)ctx;
	if (pipe(pipefd) != 0)
		return test_fail_msg("signal: pipe failed: %d", errno);
	if (install_handler(SIGUSR1, 0) != 0)
		return test_fail_msg("signal: sigaction failed: %d", errno);

	pid = fork();
	if (pid < 0)
		return test_fail_msg("signal: fork failed: %d", errno);
	if (pid == 0) {
		close(pipefd[0]);
		sleep_ms(20);
		if (kill(getppid(), SIGUSR1) != 0)
			_exit(1);
		sleep_ms(20);
		_exit(write(pipefd[1], "x", 1) == 1 ? 0 : 2);
	}
	close(pipefd[1]);

	errno = 0;
	ssize_t n = read(pipefd[0], &c, 1);

	if (n != -1 || errno != EINTR)
		return test_fail_msg("signal: read was not interrupted");
	if (caught != 1)
		return test_fail_msg("signal: interrupted read handler did not run");
	int status;

	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return test_fail_msg("signal: interrupted read child failed");
	close(pipefd[0]);
	return 0;
}

static const struct test_case signal_cases[] = {
	{ "handler", case_handler, 0, 10000 },
	{ "mask", case_mask, 0, 10000 },
	{ "suspend", case_suspend, TEST_OWN_PGRP, 10000 },
	{ "interrupt", case_interrupt, TEST_OWN_PGRP, 10000 },
	{ "read_eintr", case_read_eintr, TEST_OWN_PGRP, 10000 },
};

const struct test_suite brktest_signal_suite = {
	.name = "signal",
	.description = "handlers, masks, pending signals and interrupted syscalls",
	.cases = signal_cases,
	.nr_cases = sizeof(signal_cases) / sizeof(signal_cases[0]),
};
