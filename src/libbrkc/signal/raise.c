#include <signal.h>
#include <unistd.h>

int raise(int sig)
{
	return kill(getpid(), sig);
}

void (*signal(int signum, void (*handler)(int)))(int)
{
	struct sigaction sa = { .sa_handler = handler };
	struct sigaction old;

	if (sigaction(signum, &sa, &old) != 0)
		return SIG_ERR;
	return old.sa_handler;
}
