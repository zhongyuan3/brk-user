#ifndef _SCHED_H
#define _SCHED_H

/*
 * Minimal POSIX <sched.h>: scheduling is not configurable in BRK,
 * so only sched_yield() is offered.
 */

int sched_yield(void);

#endif
