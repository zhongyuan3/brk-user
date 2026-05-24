#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
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

int main(void)
{
	struct sigaction sa = { .sa_handler = on_sigusr1 };

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction setup");
		return 1;
	}

	if (test_sigaction_delivery() != 0)
		return 1;
	if (test_sig_ignore() != 0)
		return 1;

	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		perror("sigaction restore");
		return 1;
	}
	if (test_sigprocmask() != 0)
		return 1;
	if (test_raise() != 0)
		return 1;
	if (test_sigwinch() != 0)
		return 1;

	return 0;
}
