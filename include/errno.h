#ifndef _ERRNO_H
#define _ERRNO_H

#include <uapi/errno.h>

int *__errno_location(void);
#define errno (*__errno_location())

#endif
