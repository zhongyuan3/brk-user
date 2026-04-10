#include <ulib.h>

int errno = 0;

void _start(int argc, char **argv, char **envp)
{
	extern int main(int argc, char **argv, char **envp);
	exit(main(argc, argv, envp));
}
