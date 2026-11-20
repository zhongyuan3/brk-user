#ifndef _SYS_TIMES_H
#define _SYS_TIMES_H

#include <sys/types.h>
#include <uapi/times.h>

clock_t times(struct tms *buf);

#endif
