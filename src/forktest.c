#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[])
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
	return EXIT_SUCCESS;
}
