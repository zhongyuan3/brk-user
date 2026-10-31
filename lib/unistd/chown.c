#include <syscall.h>
#include <unistd.h>

int chown(const char *path, uid_t owner, gid_t group)
{
	return syscall(SYS_chown, path, owner, group);
}

int fchown(int fd, uid_t owner, gid_t group)
{
	return syscall(SYS_fchown, fd, owner, group);
}

int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
	(void)flags;
	return syscall(SYS_fchownat, dirfd, path, owner, group);
}
