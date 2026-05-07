#include <stdio.h>

int main(int argc, char *argv[])
{
	int i = 1;

	if (i < argc) {
		printf("%s", argv[i]);
		++i;
		for (; i < argc; ++i)
			printf(" %s", argv[i]);
	}
	putchar('\n');

	return 0;
}
