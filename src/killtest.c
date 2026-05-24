#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

int main(void)
{
	if (test_exit_status() != 0)
		return 1;
	if (test_kill_sig(SIGKILL) != 0)
		return 1;
	if (test_kill_sig(SIGTERM) != 0)
		return 1;
	return 0;
}
