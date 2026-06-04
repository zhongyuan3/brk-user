#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static void handle_sigint(int sig)
{
	(void)sig;
}

int main(void)
{
	signal(SIGINT, handle_sigint);
	while (1) {
		pid_t cpid = fork();
		if (cpid < 0) {
			perror("fork failed");
			goto fail;
		}

		if (cpid == 0) {
			char *argv[] = { "sh", 0 };
			execvp(argv[0], argv);
			perror("execve failed");
			_exit(1);
		}

		while (1) {
			pid_t wpid = wait(0);
			if (wpid == cpid) {
				printf("\nPress any key to restart shell\n");
				getchar();
				break;
			}
		}
	}

fail:
	fprintf(stderr, "failed to start shell\n");
	while (1)
		wait(0);
}
