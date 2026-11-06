#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include <sys/time.h>
#include <uapi/resource.h>

int getrusage(int who, struct rusage *usage);

#endif
