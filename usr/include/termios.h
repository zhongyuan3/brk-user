#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <sys/types.h>
#include <uapi/termios.h>

typedef __brk_cc_t cc_t;
typedef __brk_speed_t speed_t;
typedef __brk_tcflag_t tcflag_t;

struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};

#endif
