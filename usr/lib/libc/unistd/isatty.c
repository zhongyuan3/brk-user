#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*
 * A descriptor is a tty when the terminal driver accepts TCGETS on
 * it; the kernel answers ENOTTY for everything else.
 */
int isatty(int fd)
{
	struct termios t;

	return ioctl(fd, TCGETS, &t) == 0 ? 1 : 0;
}
