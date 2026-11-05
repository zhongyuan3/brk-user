#include <sys/capability.h>
#include <syscall.h>

int capget(struct cap_header *hdr, struct cap_data data[2])
{
	return syscall(SYS_capget, hdr, data);
}

int capset(struct cap_header *hdr, const struct cap_data data[2])
{
	return syscall(SYS_capset, hdr, data);
}
