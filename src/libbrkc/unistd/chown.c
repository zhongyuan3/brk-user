#include <fcntl.h>
#include <syscall.h>
#include <unistd.h>

int chown(const char *path, uid_t owner, gid_t group)
{
	return fchownat(AT_FDCWD, path, owner, group, 0);
}

int fchown(int fd, uid_t owner, gid_t group)
{
	return fchownat(fd, "", owner, group, AT_EMPTY_PATH);
}

int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
	return syscall(SYS_fchownat, dirfd, path, owner, group, flags);
}

int lchown(const char *path, uid_t owner, gid_t group)
{
	return fchownat(AT_FDCWD, path, owner, group, AT_SYMLINK_NOFOLLOW);
}
