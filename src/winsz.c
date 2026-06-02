#include <apputil.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	struct winsize ws;

	app_init(argc, argv);

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0)
		return app_fail_errno("ioctl failed");

	printf("%u %u\n", ws.ws_row, ws.ws_col);
	return APP_EXIT_OK;
}
