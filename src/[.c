#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	(void)argc;
	execvp("test", argv);
	perror("[");
	return 2;
}
