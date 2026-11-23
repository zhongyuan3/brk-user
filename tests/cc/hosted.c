#include <stdio.h>
#include <string.h>

int main(void)
{
	char buf[64];
	int n;

	n = snprintf(buf, sizeof(buf), "%s %d", "hosted", 42);
	printf("%s %d\n", buf, n);
	return 0;
}
