#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <sys/types.h>

#define NCCS 32

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

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

/* c_cc indices (Linux layout) */
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE 14
#define VLNEXT 15

/* c_iflag */
#define IGNBRK 0000001u
#define BRKINT 0000002u
#define ICRNL 0000400u
#define INLCR 0000100u
#define IGNCR 0000200u
#define IXON 0002000u
#define IXOFF 0010000u

/* c_oflag */
#define OPOST 0000001u
#define ONLCR 0000004u

/* c_lflag */
#define ISIG 0000001u
#define ICANON 0000002u
#define ECHO 0000010u
#define ECHOE 0000020u
#define ECHOK 0000040u
#define ECHOCTL 0001000u
#define TOSTOP 0000400u
#define IEXTEN 0100000u

/* ioctls */
#define TCGETS 0x5401u
#define TCSETS 0x5402u
#define TCSETSW 0x5403u
#define TCSETSF 0x5404u
#define TCFLSH 0x540Bu

#define TCIFLUSH 0
#define TCOFLUSH 1
#define TCIOFLUSH 2

#endif
