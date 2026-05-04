#include <stdlib.h>
#include <unistd.h>

void exit(int status)
{
	_exit(status);
	for (;;)
		;
}

int atexit(void (*func)(void))
{
	return 0;
}
