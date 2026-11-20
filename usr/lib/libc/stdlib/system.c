#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int system(const char *command)
{
	pid_t pid;
	int status;

	if (!command)
		return 1; /* a shell is available */

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	return status;
}
