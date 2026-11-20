#ifndef _SYS_CAPABILITY_H
#define _SYS_CAPABILITY_H

#include <sys/types.h>
#include <uapi/capability.h>

int capget(struct cap_header *hdr, struct cap_data data[2]);
int capset(struct cap_header *hdr, const struct cap_data data[2]);

#endif
