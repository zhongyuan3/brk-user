#include <stdlib.h>
#include <unistd.h>

void _start(int argc, char **argv, char **envp)
{
	extern int main(int argc, char **argv, char **envp);

	environ = envp;
	exit(main(argc, argv, envp));
}
