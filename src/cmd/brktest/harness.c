#include "harness.h"
#include "util.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

struct test_config test_config = {
	.timeout_ms = 10000,
	.workers = 4,
	.iterations = 5,
	.duration_ms = 0,
	.isolate = true,
	.format = TEST_FORMAT_HUMAN,
};

static unsigned int tap_total;

static bool tap_mode(void)
{
	return test_config.format == TEST_FORMAT_TAP;
}

unsigned int test_tap_total(void)
{
	return tap_total;
}

static unsigned long long now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (unsigned long long)ts.tv_sec * 1000ULL +
	       (unsigned long long)ts.tv_nsec / 1000000ULL;
}

static int wait_for_child(pid_t pid, int timeout_ms, bool own_pgrp,
			  bool *timed_out)
{
	unsigned long long start = now_ms();
	int status = 0;

	*timed_out = false;

	for (;;) {
		pid_t ret = waitpid(pid, &status, WNOHANG);

		if (ret == pid)
			return status;
		if (ret < 0)
			return -1;

		if (now_ms() - start >= (unsigned long long)timeout_ms) {
			*timed_out = true;
			/*
			 * Cases that opt into their own process group can be
			 * killed together with descendants.  Others stay in
			 * the harness group because they depend on terminal
			 * process-group semantics (for example SIGWINCH).
			 */
			if (own_pgrp && kill(-pid, SIGKILL) == 0)
				;
			else
				kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
			return status;
		}

		struct timespec delay = {
			.tv_sec = 0,
			.tv_nsec = 10 * 1000 * 1000,
		};
		(void)nanosleep(&delay, NULL);
	}
}

static void report_skip(const struct test_suite *suite,
			const struct test_case *tc, const char *reason)
{
	tap_total++;
	if (tap_mode()) {
		printf("ok %u - %s.%s # SKIP %s\n", tap_total, suite->name,
		       tc->name, reason);
		return;
	}
	printf("[SKIP] %s.%s (%s)\n", suite->name, tc->name, reason);
}

static void report_result(const struct test_suite *suite,
			  const struct test_case *tc, int result,
			  unsigned long long elapsed, const char *detail)
{
	tap_total++;
	if (tap_mode()) {
		printf("%s %u - %s.%s\n",
		       result == TEST_PASS ? "ok" : "not ok", tap_total,
		       suite->name, tc->name);
		if (detail)
			printf("# %s.%s: %s\n", suite->name, tc->name,
			       detail);
		return;
	}

	if (result == TEST_PASS) {
		printf("[ OK ] %s.%s (%llums)\n", suite->name, tc->name,
		       elapsed);
	} else if (detail) {
		printf("[FAIL] %s.%s (%llums, %s)\n", suite->name, tc->name,
		       elapsed, detail);
	} else {
		printf("[FAIL] %s.%s (%llums)\n", suite->name, tc->name,
		       elapsed);
	}
}

int test_run_case(const struct test_suite *suite, const struct test_case *tc,
		  struct test_ctx *ctx)
{
	unsigned int timeout_ms = tc->timeout_ms ? tc->timeout_ms :
						   test_config.timeout_ms;
	unsigned long long start;
	unsigned long long elapsed;
	int status = 0;
	bool timed_out = false;
	pid_t pid;

	if ((tc->flags & TEST_REQUIRE_ROOT) && geteuid() != 0) {
		report_skip(suite, tc, "requires root");
		return TEST_SKIP;
	}
	if ((tc->flags & TEST_DESTRUCTIVE) && !test_config.allow_destructive) {
		report_skip(suite, tc, "destructive; use --allow-destructive");
		return TEST_SKIP;
	}

	if (!tap_mode()) {
		printf("[RUN ] %s.%s\n", suite->name, tc->name);
		fflush(stdout);
	}
	start = now_ms();

	if (!test_config.isolate) {
		int ret = tc->run(ctx);

		elapsed = now_ms() - start;
		if (ret == 0) {
			report_result(suite, tc, TEST_PASS, elapsed, NULL);
			return TEST_PASS;
		}
		if (ret == TEST_SKIP_CODE) {
			report_skip(suite, tc, "test requested skip");
			return TEST_SKIP;
		}
		report_result(suite, tc, TEST_FAIL, elapsed, NULL);
		return TEST_FAIL;
	}

	pid = fork();
	if (pid < 0) {
		perror("brktest: fork");
		return TEST_FAIL;
	}
	if (pid == 0) {
		/*
		 * Only cases that opt in get a private process group.  Signal
		 * tests must stay in the terminal foreground group.
		 */
		if (tc->flags & TEST_OWN_PGRP)
			(void)syscall(SYS_setpgid, 0, 0);
		if (tap_mode()) {
			int null = open("/dev/null", O_WRONLY);

			if (null >= 0) {
				dup2(null, STDOUT_FILENO);
				dup2(null, STDERR_FILENO);
				if (null > STDERR_FILENO)
					close(null);
			}
		}
		int ret = tc->run(ctx);

		fflush(NULL);
		_exit(ret == TEST_SKIP_CODE ? TEST_SKIP_CODE :
		      ret ? 1 : 0);
	}

	status = wait_for_child(pid, timeout_ms, tc->flags & TEST_OWN_PGRP,
				&timed_out);
	elapsed = now_ms() - start;

	if (timed_out) {
		report_result(suite, tc, TEST_FAIL, elapsed, "TIMEOUT");
		return TEST_FAIL;
	}
	if (status < 0) {
		report_result(suite, tc, TEST_FAIL, elapsed,
			      "waitpid failed");
		return TEST_FAIL;
	}
	if (WIFSIGNALED(status)) {
		report_result(suite, tc, TEST_FAIL, elapsed, "signalled");
		return TEST_FAIL;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == TEST_SKIP_CODE) {
		report_skip(suite, tc, "test requested skip");
		return TEST_SKIP;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		report_result(suite, tc, TEST_FAIL, elapsed, NULL);
		return TEST_FAIL;
	}

	report_result(suite, tc, TEST_PASS, elapsed, NULL);
	return TEST_PASS;
}

int test_run_suite(const struct test_suite *suite, struct test_ctx *ctx)
{
	unsigned int passed = 0;
	unsigned int failed = 0;
	unsigned int skipped = 0;

	if (!tap_mode())
		printf("\n=== %s ===\n", suite->name);
	for (size_t i = 0; i < suite->nr_cases; i++) {
		int ret = test_run_case(suite, &suite->cases[i], ctx);

		if (ret == TEST_PASS)
			passed++;
		else if (ret == TEST_SKIP)
			skipped++;
		else
			failed++;
	}

	if (tap_mode()) {
		printf("# %s: %u passed, %u failed, %u skipped\n", suite->name,
		       passed, failed, skipped);
		return failed ? -1 : 0;
	}

	printf("--- %s: %u passed, %u failed, %u skipped\n", suite->name,
	       passed, failed, skipped);
	if (failed)
		printf("=== %s: FAILED ===\n", suite->name);
	else
		printf("=== %s: ok ===\n", suite->name);
	return failed ? -1 : 0;
}
