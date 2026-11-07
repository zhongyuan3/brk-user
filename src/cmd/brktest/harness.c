#include "harness.h"

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
	.isolate = true,
};

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
		printf("[SKIP] %s.%s (requires root)\n", suite->name, tc->name);
		return TEST_SKIP;
	}

	printf("[RUN ] %s.%s\n", suite->name, tc->name);
	fflush(stdout);
	start = now_ms();

	if (!test_config.isolate) {
		int ret = tc->run(ctx);

		elapsed = now_ms() - start;
		if (ret == 0) {
			printf("[ OK ] %s.%s (%llums)\n", suite->name, tc->name,
			       elapsed);
			return TEST_PASS;
		}
		printf("[FAIL] %s.%s (%llums)\n", suite->name, tc->name,
		       elapsed);
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
		int ret = tc->run(ctx);

		fflush(NULL);
		_exit(ret == 0 ? 0 : 1);
	}

	status = wait_for_child(pid, timeout_ms, tc->flags & TEST_OWN_PGRP,
				&timed_out);
	elapsed = now_ms() - start;

	if (timed_out) {
		printf("[FAIL] %s.%s (%llums, TIMEOUT after %ums)\n",
		       suite->name, tc->name, elapsed, timeout_ms);
		return TEST_FAIL;
	}
	if (status < 0) {
		printf("[FAIL] %s.%s (%llums, waitpid failed)\n", suite->name,
		       tc->name, elapsed);
		return TEST_FAIL;
	}
	if (WIFSIGNALED(status)) {
		printf("[FAIL] %s.%s (%llums, signal %d)\n", suite->name,
		       tc->name, elapsed, WTERMSIG(status));
		return TEST_FAIL;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("[FAIL] %s.%s (%llums)\n", suite->name, tc->name,
		       elapsed);
		return TEST_FAIL;
	}

	printf("[ OK ] %s.%s (%llums)\n", suite->name, tc->name, elapsed);
	return TEST_PASS;
}

int test_run_suite(const struct test_suite *suite, struct test_ctx *ctx)
{
	unsigned int passed = 0;
	unsigned int failed = 0;
	unsigned int skipped = 0;

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

	printf("--- %s: %u passed, %u failed, %u skipped\n", suite->name,
	       passed, failed, skipped);
	if (failed)
		printf("=== %s: FAILED ===\n", suite->name);
	else
		printf("=== %s: ok ===\n", suite->name);
	return failed ? -1 : 0;
}
