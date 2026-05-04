#include <stdlib.h>
#include <unistd.h>

#define ATEXIT_MAX 32

static void (*atexit_funcs[ATEXIT_MAX])(void);
static int atexit_count;

int atexit(void (*func)(void))
{
	if (!func || atexit_count >= ATEXIT_MAX)
		return -1;
	atexit_funcs[atexit_count++] = func;
	return 0;
}

void exit(int status)
{
	while (atexit_count > 0)
		atexit_funcs[--atexit_count]();
	_exit(status);
}
