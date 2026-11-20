#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

void abort(void)
{
	raise(SIGABRT);

	/* raise() may be ignored if the handler for SIGABRT returns; make
	 * certain the process stops by restoring the default action. */
	signal(SIGABRT, SIG_DFL);
	raise(SIGABRT);
	_exit(127);
}
