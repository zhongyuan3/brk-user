#include <brk/cmd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char usage[] = "Usage: time [-p] COMMAND [ARG...]\n"
			    "Run COMMAND and report elapsed and CPU time.";

static void print_time(const char *label, struct timeval tv)
{
	fprintf(stderr, "%s %ld.%06ld\n", label, (long)tv.tv_sec,
		(long)tv.tv_usec);
}

int main(int argc, char *argv[])
{
	struct timespec start, end;
	struct rusage ru;
	pid_t pid;
	int status;
	int index = 1;

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(usage);
	if (index < argc && !strcmp(argv[index], "-p"))
		index++;
	if (index >= argc)
		brk_cmd_usage(usage);

	if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
		return brk_cmd_fail_errno("clock_gettime");

	pid = fork();
	if (pid < 0)
		return brk_cmd_fail_errno("fork");
	if (pid == 0) {
		execvp(argv[index], &argv[index]);
		fprintf(stderr, "time: %s: %s\n", argv[index], strerror(errno));
		_exit(127);
	}

	if (waitpid(pid, &status, 0) != pid)
		return brk_cmd_fail_errno("waitpid");
	if (clock_gettime(CLOCK_MONOTONIC, &end) != 0)
		return brk_cmd_fail_errno("clock_gettime");
	if (getrusage(RUSAGE_CHILDREN, &ru) != 0)
		return brk_cmd_fail_errno("getrusage");

	long sec = end.tv_sec - start.tv_sec;
	long nsec = end.tv_nsec - start.tv_nsec;

	if (nsec < 0) {
		sec--;
		nsec += 1000000000L;
	}

	fprintf(stderr, "real %ld.%06ld\n", sec, nsec / 1000);
	print_time("user", ru.ru_utime);
	print_time("sys ", ru.ru_stime);

	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return BRK_CMD_EXIT_FAIL;
}
