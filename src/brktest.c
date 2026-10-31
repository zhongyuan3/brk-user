#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile int caught;

static void on_sigusr1(int sig)
{
	(void)sig;
	caught = 1;
}

static void on_sigwinch(int sig)
{
	(void)sig;
	caught = sig;
}

static int test_fork(void)
{
	puts("Creating process...");
	for (int i = 0; i < 5; ++i) {
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			continue;
		}
		if (pid == 0) {
			for (int j = 0; j < 300; ++j) {
				char c = 'A' + i;
				write(STDOUT_FILENO, &c, 1);
			}
			_exit(0);
		}
	}
	while (1) {
		if (wait(0) < 0)
			break;
	}
	putchar('\n');
	return 0;
}

static int test_exit_status(void)
{
	pid_t pid = fork();
	int status;

	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0)
		exit(42);

	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		return -1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 42) {
		printf("exit status test failed: status=0x%x\n", status);
		return -1;
	}
	printf("exit status ok: %d\n", WEXITSTATUS(status));
	return 0;
}

static int test_kill_sig(int sig)
{
	pid_t pid = fork();
	int status;
	struct timespec ts = { .tv_sec = 1000, .tv_nsec = 0 };

	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		nanosleep(&ts, NULL);
		exit(0);
	}

	if (kill(pid, sig) != 0) {
		perror("kill");
		return -1;
	}

	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		return -1;
	}
	if (!WIFSIGNALED(status) || WTERMSIG(status) != sig) {
		printf("kill(%d) test failed: status=0x%x\n", sig, status);
		return -1;
	}
	printf("kill sig %d ok\n", sig);
	return 0;
}

static int test_kill(void)
{
	if (test_exit_status() != 0)
		return -1;
	if (test_kill_sig(SIGKILL) != 0)
		return -1;
	if (test_kill_sig(SIGTERM) != 0)
		return -1;
	return 0;
}

static int test_raise(void)
{
	struct sigaction sa = { .sa_handler = on_sigusr1 };

	caught = 0;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction");
		return -1;
	}
	if (raise(SIGUSR1) != 0) {
		perror("raise");
		return -1;
	}
	if (!caught) {
		printf("raise: handler did not run\n");
		return -1;
	}
	printf("raise ok\n");
	return 0;
}

static int test_sigwinch(void)
{
	struct sigaction sa = { .sa_handler = on_sigwinch };
	struct winsize ws;

	caught = 0;
	if (sigaction(SIGWINCH, &sa, NULL) != 0) {
		perror("sigaction SIGWINCH");
		return -1;
	}
	if (kill(getpid(), SIGWINCH) != 0) {
		perror("kill SIGWINCH");
		return -1;
	}
	if (caught != SIGWINCH) {
		printf("sigwinch kill: handler did not run (caught=%d)\n",
		       caught);
		return -1;
	}

	caught = 0;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
		perror("TIOCGWINSZ");
		return -1;
	}
	ws.ws_col = ws.ws_col == 100 ? 101 : 100;
	if (ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws) != 0) {
		perror("TIOCSWINSZ");
		return -1;
	}
	if (caught != SIGWINCH) {
		printf("sigwinch ioctl: handler did not run (caught=%d)\n",
		       caught);
		return -1;
	}
	printf("sigwinch ok\n");
	return 0;
}

static int test_sigaction_delivery(void)
{
	struct sigaction sa = { .sa_handler = on_sigusr1 };

	caught = 0;
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction");
		return -1;
	}

	if (kill(getpid(), SIGUSR1) != 0) {
		perror("kill");
		return -1;
	}

	if (!caught) {
		printf("sigaction: handler did not run\n");
		return -1;
	}
	printf("sigaction delivery ok\n");
	return 0;
}

static int test_sig_ignore(void)
{
	struct sigaction sa = { .sa_handler = SIG_IGN };

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction ignore");
		return -1;
	}

	if (kill(getpid(), SIGUSR1) != 0) {
		perror("kill");
		return -1;
	}

	printf("sigignore ok\n");
	return 0;
}

static int test_sigprocmask(void)
{
	sigset_t set = (1ULL << SIGUSR1);
	sigset_t old;

	if (sigprocmask(SIG_BLOCK, &set, &old) != 0) {
		perror("sigprocmask block");
		return -1;
	}

	caught = 0;
	if (kill(getpid(), SIGUSR1) != 0) {
		perror("kill");
		return -1;
	}

	if (caught) {
		printf("sigprocmask: signal delivered while blocked\n");
		return -1;
	}

	if (sigprocmask(SIG_SETMASK, &old, NULL) != 0) {
		perror("sigprocmask restore");
		return -1;
	}

	if (!caught) {
		printf("sigprocmask: signal not delivered after unblock\n");
		return -1;
	}

	printf("sigprocmask ok\n");
	return 0;
}

static int test_signal(void)
{
	struct sigaction sa = { .sa_handler = on_sigusr1 };

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction setup");
		return -1;
	}

	if (test_sigaction_delivery() != 0)
		return -1;
	if (test_sig_ignore() != 0)
		return -1;

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction restore");
		return -1;
	}
	if (test_sigprocmask() != 0)
		return -1;
	if (test_raise() != 0)
		return -1;
	if (test_sigwinch() != 0)
		return -1;

	return 0;
}

struct test_suite {
	const char *name;
	int (*run)(void);
};

static const struct test_suite suites[] = {
	{ "fork", test_fork },
	{ "kill", test_kill },
	{ "sig", test_signal },
};

#define NR_SUITES (sizeof(suites) / sizeof(suites[0]))

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [suite|all]\n\n"
		"Suites:\n"
		"  fork   process creation and wait\n"
		"  kill   exit status and signal delivery\n"
		"  sig    signal handlers, masks and SIGWINCH\n"
		"  all    run every suite (default)\n",
		prog);
}

static int run_suite(const struct test_suite *suite)
{
	int ret;

	printf("\n=== %s ===\n", suite->name);
	ret = suite->run();
	if (ret == 0) {
		printf("=== %s: ok ===\n", suite->name);
		return 0;
	}
	printf("=== %s: FAILED ===\n", suite->name);
	return -1;
}

int main(int argc, char *argv[])
{
	const char *what = argc > 1 ? argv[1] : "all";
	int failures = 0;
	int total = 0;

	if (!strcmp(what, "-h") || !strcmp(what, "--help")) {
		usage(argv[0]);
		return 0;
	}

	if (!strcmp(what, "all")) {
		total = (int)NR_SUITES;
		for (size_t i = 0; i < NR_SUITES; i++)
			if (run_suite(&suites[i]) != 0)
				failures++;
	} else {
		for (size_t i = 0; i < NR_SUITES; i++) {
			if (!strcmp(what, suites[i].name)) {
				total = 1;
				if (run_suite(&suites[i]) != 0)
					failures++;
				goto done;
			}
		}
		fprintf(stderr, "%s: unknown suite: %s\n\n", argv[0], what);
		usage(argv[0]);
		return 1;
	}

done:
	printf("\n%d suite(s), %d failed\n", total, failures);
	return failures ? 1 : 0;
}
