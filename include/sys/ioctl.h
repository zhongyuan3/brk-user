#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include <sys/types.h>
#include <uapi/ioctl.h>

int ioctl(int fd, int request, ...);

#endif
