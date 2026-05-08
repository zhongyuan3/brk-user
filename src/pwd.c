#include <stdio.h>
#include <unistd.h>

int main(void)
{
	char buf[1024];
	if (getcwd(buf, sizeof(buf)) == NULL) {
		perror("getcwd");
		return 1;
	}
	buf[sizeof(buf) - 1] = '\0';
	puts(buf);
	return 0;
}
