#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <brk/resource.h>
#include <sys/types.h>

pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage);
pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);

#endif
