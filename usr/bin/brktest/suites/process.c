#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int case_status(struct test_ctx *ctx)
{
	int status;
	pid_t pid;

	(void)ctx;
	pid = fork();
	if (pid < 0)
		return test_fail_msg("process: fork failed: %d", errno);
	if (pid == 0)
		_exit(42);
	if (waitpid(pid, &status, 0) != pid)
		return test_fail_msg("process: waitpid failed: %d", errno);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 42)
		return test_fail_msg("process: exit status is 0x%x", status);

	pid = fork();
	if (pid < 0)
		return test_fail_msg("process: fork failed: %d", errno);
	if (pid == 0) {
		for (;;)
			pause();
	}
	if (kill(pid, SIGTERM) != 0)
		return test_fail_msg("process: kill failed: %d", errno);
	if (waitpid(pid, &status, 0) != pid)
		return test_fail_msg("process: waitpid failed: %d", errno);
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM)
		return test_fail_msg("process: signal status is 0x%x", status);

	pid = fork();
	if (pid < 0)
		return test_fail_msg("process: fork failed: %d", errno);
	if (pid == 0)
		_exit(0);

	for (;;) {
		pid_t ret = waitpid(pid, &status, WNOHANG);

		if (ret == pid)
			break;
		if (ret < 0)
			return test_fail_msg("process: waitpid(WNOHANG): %d",
					     errno);
		struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 };

		(void)nanosleep(&delay, NULL);
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return test_fail_msg("process: reaped status is 0x%x", status);

	errno = 0;
	if (waitpid(pid, &status, WNOHANG) != -1 || errno != ECHILD)
		return test_fail_msg("process: expected ECHILD, got %d", errno);
	return 0;
}

static int case_waitid(struct test_ctx *ctx)
{
	siginfo_t info;
	pid_t pid;

	(void)ctx;
	pid = fork();
	if (pid < 0)
		return test_fail_msg("process: fork failed: %d", errno);
	if (pid == 0)
		_exit(23);

	memset(&info, 0, sizeof(info));
	if (waitid(P_PID, pid, &info, WEXITED) != 0)
		return test_fail_msg("process: waitid failed: %d", errno);
	if (info.si_pid != pid || info.si_signo != SIGCHLD ||
	    info.si_code != CLD_EXITED || info.si_status != 23)
		return test_fail_msg("process: waitid info is wrong");
	return 0;
}

static int case_groups(struct test_ctx *ctx)
{
	int status;
	pid_t pid;

	(void)ctx;
	pid = fork();
	if (pid < 0)
		return test_fail_msg("process: fork failed: %d", errno);
	if (pid == 0) {
		pid_t self = getpid();

		if (getpgrp() == self)
			_exit(10);
		if (setsid() != self)
			_exit(11);
		if (getsid(0) != self || getpgrp() != self ||
		    getpgid(0) != self)
			_exit(12);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return test_fail_msg("process: session creation failed");

	pid = fork();
	if (pid < 0)
		return test_fail_msg("process: fork failed: %d", errno);
	if (pid == 0) {
		pid_t self = getpid();

		if (setpgid(0, 0) != 0)
			_exit(20);
		if (getpgrp() != self || getpgid(0) != self)
			_exit(21);
		_exit(0);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0)
		return test_fail_msg("process: process group creation failed");
	return 0;
}

static int case_exec_fail(struct test_ctx *ctx)
{
	char *argv[] = { "missing", NULL };
	char *envp[] = { NULL };
	int status;
	pid_t pid;

	(void)ctx;
	pid = fork();
	if (pid < 0)
		return test_fail_msg("process: fork failed: %d", errno);
	if (pid == 0) {
		execve("/no/such/brktest-program", argv, envp);
		_exit(errno == ENOENT ? 0 : 1);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return test_fail_msg("process: exec failure status is 0x%x",
				     status);
	if (WEXITSTATUS(status) != 0)
		return test_fail_msg("process: exec failure errno is wrong");
	return 0;
}

static void *idle_thread(void *arg)
{
	(void)arg;
	for (;;)
		pause();
	return NULL;
}

static int case_exit_group(struct test_ctx *ctx)
{
	pthread_t thread;
	int status;
	pid_t pid;

	(void)ctx;
	pid = fork();
	if (pid < 0)
		return test_fail_msg("process: fork failed: %d", errno);
	if (pid == 0) {
		if (pthread_create(&thread, NULL, idle_thread, NULL) != 0)
			_exit(10);
		struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000 };

		(void)nanosleep(&delay, NULL);
		_exit(9);
	}
	if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status))
		return test_fail_msg("process: exit_group status is 0x%x",
				     status);
	if (WEXITSTATUS(status) != 9)
		return test_fail_msg("process: exit_group returned %d",
				     WEXITSTATUS(status));
	return 0;
}

static const struct test_case process_cases[] = {
	{ "status", case_status, TEST_OWN_PGRP, 10000 },
	{ "waitid", case_waitid, TEST_OWN_PGRP, 10000 },
	{ "groups", case_groups, TEST_OWN_PGRP, 10000 },
	{ "exec_fail", case_exec_fail, TEST_OWN_PGRP, 10000 },
	{ "exit_group", case_exit_group, TEST_OWN_PGRP, 10000 },
};

const struct test_suite brktest_process_suite = {
	.name = "process",
	.description = "fork, wait, exit status and process groups",
	.cases = process_cases,
	.nr_cases = sizeof(process_cases) / sizeof(process_cases[0]),
};
