#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
		perror("ioctl");
		return 1;
	}
	printf("%u %u\n", ws.ws_row, ws.ws_col);
	return 0;
}
