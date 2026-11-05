#include <sys/mount.h>
#include <syscall.h>

int mount(const char *source, const char *target, const char *filesystemtype,
	  unsigned long mountflags, const void *data)
{
	return syscall(SYS_mount, source, target, filesystemtype, mountflags,
		       data);
}

int umount2(const char *target, int flags)
{
	return syscall(SYS_umount2, target, flags);
}
