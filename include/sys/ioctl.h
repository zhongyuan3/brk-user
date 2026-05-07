#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include <sys/types.h>

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#define TIOCGWINSZ 0x5413u
#define TIOCSWINSZ 0x5414u

int ioctl(int fd, int request, ...);

#endif
