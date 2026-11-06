#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/resource.h>
#include <sys/types.h>
#include <uapi/signal.h>

typedef int idtype_t;

pid_t wait(int *wstatus);
pid_t waitpid(pid_t pid, int *wstatus, int options);
int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options);

#endif
